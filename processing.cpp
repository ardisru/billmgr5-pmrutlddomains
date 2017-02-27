#include <defines.h>
#include <mgr/mgrhash.h>
#include <mgr/mgrlog.h>
#include <mgr/mgrregex.h>
#include <mgr/mgrrpc.h>
#include <processing/domain_common.h>
#include <processing/processingmodule.h>
#include <table/dbobject.h>

#include <fstream>

#include "json11/json11.hpp"

// These variables are defined using build system in config.mk

// Processing module name.
// #define BINARY_NAME "pmrutlddomains"

// Default URL.
// #define RUTLD_PROD_URL "https://my.ru-tld.ru/manager/billmgr"

// Project names to find the correct account ID.
// #define RUTLD_PROJECT_NAME "*.ru-tld.ru (Domains)"

// Item param name to store billmgr4 domain id.
#define PARAM_REMOTE_ID "b4_remote_id"
// Item param name to store billmgr4 pricelist id.
#define PARAM_REMOTE_PRICE "b4_remote_price"

// Table name to store contact mapping.
#define CONTACT_MAPPING_TABLE_NAME "b4_contact_mapping"

// Registrar IDs.
#define RUTLD_PROD_NIC_REGISTRAR_ID 5
#define RUTLD_PROD_ARDIS_REGISTRAR_ID 13

// List of zones with one contact only.
#define RUSSIAN_ZONES \
  { "ru", "su", "рф", "ru.net", "москва", "moscow" }

MODULE(BINARY_NAME);

using namespace processing;
using json11::Json;

namespace {

struct DomainPrice {
  string tld;
  int id = -1;
  int registrar_id = -1;
  string name;
  int priority = -1;
  std::map<int, int> periods;
  bool is_ru = false;
  bool is_nic = false;
  double one_year_price = -1;
};

const Json& NotNull(const Json& item, const string& key) {
  const Json& ret = item[key];
  if (ret.is_null()) {
    throw mgr_err::Missed(key);
  }
  return ret;
}

int GetJsonInt(const Json& item) {
  if (item.is_number()) {
    return item.int_value();
  } else if (item.is_string()) {
    int rv = str::Int(item.string_value());
    if (str::Str(rv) != item.string_value()) {
      throw mgr_err::Error("json_string_not_an_int");
    }
    return rv;
  } else {
    throw mgr_err::Error("json_bad_int_type");
  }
}

int GetJsonInt(const Json& parent, const string& key) {
  return GetJsonInt(NotNull(parent, key));
}

Json ReadJsonFromFile(const string& path) {
  string content_string = mgr_file::Read(path);
  string json_parse_error;
  Json content = Json::parse(content_string, json_parse_error);
  if (content.is_null()) {
    throw mgr_err::Value("json", json_parse_error);
  }
  return content;
}

std::set<int> g_enabled_registrars;

std::map<string, std::vector<DomainPrice>> GetTldPrices() {
  // TODO: embed this file in the binary?
  Json content = ReadJsonFromFile("etc/" SHORT_NAME "_domainprice.json");
  if (!content.is_array()) {
    throw mgr_err::Value("json", "Not an array");
  }
  std::vector<DomainPrice> items;
  for (const auto& json_item : content.array_items()) {
    DomainPrice item;
    item.tld = NotNull(json_item, "tld").string_value();
    item.id = GetJsonInt(json_item, "id");
    item.registrar_id = GetJsonInt(json_item, "registrar_id");
    g_enabled_registrars.insert(item.registrar_id);
    item.name = NotNull(json_item, "name").string_value();
    item.priority = GetJsonInt(json_item, "priority");
    for (const auto& json_period : NotNull(json_item, "period").array_items()) {
      if (json_period["per_type"].string_value() != "year") {
        Warning("Skipping period type %s for price %d",
                json_period["per_type"].string_value().c_str(), item.id);
        continue;
      }
      item.periods[GetJsonInt(json_period, "p_length")] =
          GetJsonInt(json_period, "id");
      if (GetJsonInt(json_period, "p_length") == 1) {
        item.one_year_price =
            str::Double(NotNull(json_period, "price_num").string_value());
      }
    }
    if (item.registrar_id == RUTLD_PROD_NIC_REGISTRAR_ID) {
      item.is_nic = true;
    }
    for (const auto& i : RUSSIAN_ZONES) {
      if (item.tld == i || str::EndsWith(item.tld, "." + string(i))) {
        item.is_ru = true;
      }
    }
    items.emplace_back(std::move(item));
  }

  std::sort(items.begin(), items.end(), [](const DomainPrice& lhs,
                                           const DomainPrice& rhs) {
    // Ardis domains go first.
    bool lhs_ardis = lhs.registrar_id == RUTLD_PROD_ARDIS_REGISTRAR_ID,
         rhs_ardis = rhs.registrar_id == RUTLD_PROD_ARDIS_REGISTRAR_ID;
    if (lhs_ardis != rhs_ardis) return lhs_ardis > rhs_ardis;
    // Cheapest domains go first.
    if (lhs.one_year_price != rhs.one_year_price)
      return lhs.one_year_price < rhs.one_year_price;
    // Id is unique, the domain with least id goes first.
    return lhs.id < rhs.id;
  });

  std::map<string, std::vector<DomainPrice>> tld_prices;
  for (const auto& item : items) {
    tld_prices[item.tld].push_back(item);
  }
  return tld_prices;
}

string GetRemoteCountryId(const string& iso2) {
  static const StringMap map = []() {
    StringMap ret;
    Json content = ReadJsonFromFile("etc/" SHORT_NAME "_countries.json");
    for (const auto& country : content["elem"].array_items()) {
      ret[country["iso2"].string_value()] = country["id"].string_value();
    }
    return ret;
  }();
  auto it = map.find(iso2);
  return it != map.end() ? it->second
                         : throw mgr_err::Missed("remote_country", iso2);
}

string GetRemoteCountryIso2(const string &id) {
  static const StringMap map = []() {
    StringMap ret;
    Json content = ReadJsonFromFile("etc/" SHORT_NAME "_countries.json");
    for (const auto& country : content["elem"].array_items()) {
      ret[country["id"].string_value()] = country["iso2"].string_value();
    }
    return ret;
  }();
  auto it = map.find(id);
  return it != map.end() ? it->second
                         : throw mgr_err::Missed("remote_country", id);
}

string GetCountryIso2(const string& local_id) {
  return sbin::DB()
      ->Query("SELECT iso2 FROM country WHERE id = " +
              sbin::DB()->EscapeValue(local_id))
      ->Str();
}

string GetCountryId(const string& iso2) {
  return sbin::DB()
      ->Query("SELECT id FROM country WHERE iso2 = " +
              sbin::DB()->EscapeValue(iso2))
      ->Str();
}

// We cannot use billmanager externalid mapping for contacts as one local
// contact id should match exactly two remote contacts generic and not generic
// ones. We cannot tie it to billmanager contact type (owner / tech / bill /
// ...) as owner will be a generic contact for some zones and not generic for
// other zones.
// Hence, we will create a separate table in billmanager database to track these
// matches. We don't use mgr_db::Table as we don't want to add a plugin to the
// billmgr process.

mgr_db::Connection* GetContactDbConnection() {
  // TODO: We need a separate database connection not to interfere with JobCache
  // transactions
  static mgr_db::Connection* connection = sbin::DB()->GetConnection();
  static bool ensured = false;
  if (!ensured &&
      !connection->Query("SHOW TABLES LIKE '" CONTACT_MAPPING_TABLE_NAME "'")
           ->First()) {
    connection->Query(
        "CREATE TABLE " CONTACT_MAPPING_TABLE_NAME
        "(processingmodule int(11) NOT NULL, "
        "service_profile int(11) NOT NULL, "
        "is_generic bool NOT NULL, "
        "externalid varchar(64) NOT NULL, "
        "PRIMARY KEY (processingmodule, service_profile, is_generic)) "
        "ENGINE=InnoDB DEFAULT CHARSET=utf8");
  }
  ensured = true;
  return connection;
}

string GetRemoteContactId(int local_id, int processing_module,
                          bool is_generic) {
  return GetContactDbConnection()
      ->Query(
          "SELECT externalid FROM " CONTACT_MAPPING_TABLE_NAME
          " WHERE processingmodule=? AND service_profile=? AND is_generic=?",
          str::Str(processing_module), str::Str(local_id), str::Str(is_generic))
      ->Str();
}

int GetLocalContactId(int processing_module, const string& remote_id) {
  auto cursor = GetContactDbConnection()->Query(
      "SELECT service_profile FROM " CONTACT_MAPPING_TABLE_NAME
      " WHERE processingmodule=? AND externalid=?",
      str::Str(processing_module), remote_id);
  return cursor->First() ? cursor->Int() : -1;
}

void SetRemoteContactId(int local_id, int processing_module, bool is_generic,
                        const string& remote_id) {
  GetContactDbConnection()->Query("INSERT INTO " CONTACT_MAPPING_TABLE_NAME
                                  " (processingmodule, service_profile, "
                                  "is_generic, externalid) VALUES (?,?,?,?)",
                                  str::Str(processing_module),
                                  str::Str(local_id), str::Str(is_generic),
                                  remote_id);
}

class CLASS_NAME : public Registrator {
 private:
  string username_;
  string password_;
  string url_;
  int allowed_registrar_ = -1;
  const std::map<string, std::vector<DomainPrice>> tld_prices_;
  std::unique_ptr<mgr_client::Remote> client_;
  int processing_module_ = 0;

  static StringVector GetNsVector(const StringMap& item_params) {
    StringVector ns;
    for (int i = 0; i < 4; ++i) {
      auto it = item_params.find("ns" + str::Str(i));
      if (it != item_params.end() && !it->second.empty()) {
        ns.push_back(it->second);
      }
    }
    return ns;
  }

  const DomainPrice& GetDomainPrice(int pricelist, const string& tld,
                                    int advise = -1) {
    if (advise == -1 && allowed_registrar_ != -1) {
      return tld_prices_.at(tld).at(0);
    } else {
      for (const auto& i : tld_prices_.at(tld)) {
        if ((i.id == advise || advise == -1) &&
            i.registrar_id == allowed_registrar_) {
          return i;
        }
      }
      throw mgr_err::Error("wrong_pricelist_id_for_tld");
    }
  }

  mgr_client::Result Remote_MakeRequest(StringMap params_copy) {
    LogExt("Performing request: \n%s\n",
           str::JoinParams(params_copy, "\n", " = ").c_str());
    mgr_client::Result ret = client_->Query("", params_copy);
    LogExt("Response: \n%s\n", ret.xml.Str().c_str());
    return ret;
  }

  int Remote_GetAccount() {
    auto result = Remote_MakeRequest({{"func", "accountinfo"}});
    for (auto elem : result.elems()) {
      if (elem.FindNode("project").Str() == RUTLD_PROJECT_NAME) {
        return str::Int(elem.FindNode("id").Str());
      }
    }
    throw mgr_err::Missed("account_for_project");
  }

  string Remote_CreateContact(bool is_generic, const StringMap& params) {
    Debug("Func: Remote_CreateContact");

    static StringMap phone_replace_map{{"-", ""}, {"(", ""}, {")", ""}};

    string remote_name =
        "Remote " + params.at("id") + (is_generic ? " (generic)" : "");
    int local_type = str::Int(params.at("profiletype"));
    string remote_type =
        is_generic ? "generic"
                   : (local_type == table::Profile::prPersonal ||
                              local_type == table::Profile::prSoleProprietor
                          ? "person"
                          : "company");
    string remote_id = Remote_MakeRequest({{"func", "contcat.create.1"},
                                           {"ctype", remote_type},
                                           {"cname", remote_name},
                                           {"sok", "ok"}})
                           .value("domaincontact.id");

    StringMap request;
    request["func"] = "domaincontact.edit";
    request["sok"] = "ok";
    request["elid"] = remote_id;
    request["name"] = remote_name;
    request["ctype"] = remote_type;
    auto copy = [&request, &params](const string& dst, const string& src = "") {
      request[dst] = params.at(!src.empty() ? src : dst);
    };

    // Common fields for all types: location address, email, phone
    copy("email");
    request["phone"] = str::Replace(params.at("phone"), phone_replace_map);
    request["fax"] = str::Replace(params.at("fax"), phone_replace_map);
    request["la_country"] =
        GetRemoteCountryId(GetCountryIso2(params.at("location_country")));
    copy("la_state", "location_state");
    copy("la_postcode", "location_postcode");
    copy("la_city", "location_city");
    copy("la_address", "location_address");

    // Common fields for all russian types: postal address, mobile phone
    if (remote_type != "generic") {
      request["pa_country"] =
          GetRemoteCountryId(GetCountryIso2(params.at("postal_country")));
      copy("pa_state", "postal_state");
      copy("pa_postcode", "postal_postcode");
      copy("pa_city", "postal_city");
      copy("pa_address", "postal_address");
      copy("pa_addressee", "postal_addressee");
      request["mobile"] =
          str::Replace(!params.at("mobile").empty() ? params.at("mobile")
                                                    : params.at("phone"),
                       phone_replace_map);
    }

    if (remote_type == "person") {
      copy("firstname_ru", "firstname_locale");
      copy("middlename_ru", "middlename_locale");
      copy("lastname_ru", "lastname_locale");
      copy("firstname");
      copy("middlename");
      copy("lastname");
      if (local_type == table::Profile::prSoleProprietor) {
        copy("inn");
      }
      copy("birthdate");
      copy("passport_series", "passport");
      copy("passport_org");
      copy("passport_date");
    } else if (remote_type == "company") {
      copy("company");
      copy("company_ru", "company_locale");
      copy("inn");
      copy("kpp");
      copy("ogrn");
    } else if (remote_type == "generic") {
      if (local_type == table::Profile::prPersonal) {
        request["company"] = "N/A";
      } else if (local_type == table::Profile::prSoleProprietor) {
        request["company"] =
            "IP " + params.at("firstname") + " " + params.at("lastname");
      } else {
        copy("company");
      }
      copy("firstname");
      copy("lastname");
    }
    Remote_MakeRequest(request);
    return remote_id;
  }

  string CreateRemoteContact(int module, bool is_generic,
                             const StringMap& params) {
    string remote_id =
        GetRemoteContactId(str::Int(params.at("id")), module, is_generic);
    if (remote_id.empty()) {
      remote_id = Remote_CreateContact(is_generic, params);
      SetRemoteContactId(str::Int(params.at("id")), module, is_generic,
                         remote_id);
    }
    return remote_id;
  }

  StringMap CreateRemoteContacts(int module, int item,
                                 const DomainPrice& price) {
    StringMap remote_contacts;
    if (price.is_nic) {
      remote_contacts["customer"] =
          CreateRemoteContact(module, false, ServiceProfile(item, "owner"));
    }

    if (price.is_ru) {
      remote_contacts["owner"] =
          CreateRemoteContact(module, false, ServiceProfile(item, "owner"));
    } else {
      remote_contacts["owner"] =
          CreateRemoteContact(module, true, ServiceProfile(item, "owner"));
      remote_contacts["admin"] =
          CreateRemoteContact(module, true, ServiceProfile(item, "admin"));
      remote_contacts["bill"] =
          CreateRemoteContact(module, true, ServiceProfile(item, "bill"));
      remote_contacts["tech"] =
          CreateRemoteContact(module, true, ServiceProfile(item, "tech"));
    }
    return remote_contacts;
  }

  string Remote_Open(const string& domain, const DomainPrice& price, int period,
                     const StringMap& contacts, const StringVector& ns) {
    StringMap request{{"func", "domain.order.4"},
                      {"sok", "ok"},
                      {"paynow", "on"},
                      {"countdomain", "1"},
                      {"operation", "register"}};

    string domain_without_tld =
        domain.substr(0, domain.size() - price.tld.size() - 1);
    Debug("domain_without_tld=%s", domain_without_tld.c_str());
    if (domain_without_tld + "." + price.tld != domain) {
      throw mgr_err::Error("domain_and_tld_does_not_match");
    }

    request["domain"] = domain_without_tld;
    request["domainname_0"] = domain_without_tld;
    request["tld"] = price.tld;
    Warning("About to request period %d", period);
    for (const auto& i : price.periods) {
      Warning("Avail %d %d", i.first, i.second);
    }
    request["price"] = str::Str(price.id);
    request["pricelist_0"] = str::Str(price.id);
    request["period_0"] = str::Str(price.periods.at(period));
    request["registrar"] = str::Str(price.registrar_id);
    request["payfrom"] = "account" + str::Str(Remote_GetAccount());
    for (const auto& contact : contacts) {
      request[contact.first] = contact.second;
    }
    request["nslist_0"] = str::Join(ns, " ");
    return Remote_MakeRequest(request).value("item.id");
  }

 public:
  CLASS_NAME() : Registrator(BINARY_NAME), tld_prices_(GetTldPrices()) {}

  mgr_xml::Xml Features() override {
    mgr_xml::Xml xml;
    auto itemtypes = xml.GetRoot().AppendChild("itemtypes");
    itemtypes.AppendChild("itemtype").SetProp("name", "domain");

    auto params = xml.GetRoot().AppendChild("params");
    params.AppendChild("param").SetProp("name", "username");
    params.AppendChild("param").SetProp("name", "url");
    params.AppendChild("param")
        .SetProp("name", "password")
        .SetProp("crypted", "yes");
    params.AppendChild("param").SetProp("name", "registrar");

    auto features = xml.GetRoot().AppendChild("features");
    features.AppendChild("feature").SetProp("name",
                                            PROCESSING_CHECK_CONNECTION);
    features.AppendChild("feature").SetProp("name", PROCESSING_SERVICE_IMPORT);
    features.AppendChild("feature").SetProp("name",
                                            PROCESSING_CONNECTION_FORM_TUNE);
    features.AppendChild("feature").SetProp("name",
                                            PROCESSING_GET_CONTACT_TYPE);
    // TODO: support transfer
    // features.AppendChild("feature").SetProp("name",
    // PROCESSING_DOMAIN_TRANSFER);
    features.AppendChild("feature").SetProp("name",
                                            PROCESSING_DOMAIN_UPDATE_NS);
    features.AppendChild("feature").SetProp("name", PROCESSING_PROLONG);
    features.AppendChild("feature").SetProp("name", PROCESSING_SYNC_ITEM);

    return xml;
  }

  mgr_xml::Xml GetContactType(const string& tld) override {
    mgr_xml::Xml out;
    out.GetRoot().SetProp("ns", "require").SetProp("auth_code", "require");

    const auto& tld_prices = tld_prices_.at(tld);

    if (tld_prices.at(0).is_ru) {
      out.GetRoot().AppendChild("contact_type", "owner").SetProp("main", "yes");
    } else {
      out.GetRoot().AppendChild("contact_type", "owner");
      out.GetRoot().AppendChild("contact_type", "admin");
      out.GetRoot().AppendChild("contact_type", "tech");
      out.GetRoot().AppendChild("contact_type", "bill");
    }

    return out;
  }

  void OnSetModule(const int module) override {
    Debug("Func: OnSetModule");
    processing_module_ = module;
    username_ = m_module_data["username"];
    password_ = m_module_data["password"];
    url_ = m_module_data["url"];
    if (url_.empty()) {
      url_ = RUTLD_PROD_URL;
    }

    client_.reset(new mgr_client::Remote(url_));
    client_->AddParam("authinfo", username_ + ":" + password_);
  }

  void CheckConnection(mgr_xml::Xml module_xml) override {
    Debug("Func: CheckConnection");
    m_module_data["url"] =
        module_xml.GetNode("/doc/processingmodule/url").Str();
    m_module_data["username"] =
        module_xml.GetNode("/doc/processingmodule/username").Str();
    m_module_data["password"] =
        module_xml.GetNode("/doc/processingmodule/password").Str();

    OnSetModule(0);

    Remote_GetAccount();
  }

  void Open(const int iid) override {
    Debug("Func: Open");
    auto item_query = ItemQuery(iid);
    SetModule(item_query->AsInt("processingmodule"));

    StringMap item_params;
    AddItemParam(item_params, iid);
    AddItemAddon(item_params, iid, item_query->AsInt("pricelist"));
    AddTldParam(item_params, iid);

    for (auto i : item_params) {
      Warning("Item param %s=%s", i.first.c_str(), i.second.c_str());
    }
    for (auto i : ServiceProfile(iid, "owner")) {
      Warning("Profile param %s=%s", i.first.c_str(), i.second.c_str());
    }

    const auto& remote_price = GetDomainPrice(item_query->AsInt("pricelist"),
                                              item_params.at("tld_name"));

    StringVector ns = GetNsVector(item_params);

    string remote_id = Remote_Open(
        item_params.at("domain"), remote_price,
        item_query->AsInt("period") / 12,
        CreateRemoteContacts(processing_module_, iid, remote_price), ns);

    SaveParam(iid, PARAM_REMOTE_ID, remote_id);
    SaveParam(iid, PARAM_REMOTE_PRICE, str::Str(remote_price.id));

    sbin::ClientQuery("func=" + item_query->AsString("intname") +
                      ".open&sok=ok&elid=" + str::Str(iid));

    SyncItem(iid);
  }

  void Transfer(const int iid, StringMap& transfer_notify_request) override {
    throw mgr_err::Error("not_implemented");
  }

  void Prolong(const int iid) override {
    Debug("Func: Prolong");

    auto item_query = ItemQuery(iid);
    SetModule(item_query->AsInt("processingmodule"));

    StringMap item_params;
    AddItemParam(item_params, iid);
    AddTldParam(item_params, iid);

    const auto& remote_price = GetDomainPrice(
        item_query->AsInt("pricelist"), item_params.at("tld_name"),
        str::Int(item_params.at(PARAM_REMOTE_PRICE)));

    Remote_MakeRequest(
        {{"func", "domain.renew"},
         {"sok", "ok"},
         {"elid", item_params.at(PARAM_REMOTE_ID)},
         {"paynow", "on"},
         {"payfrom", "account" + str::Str(Remote_GetAccount())},
         {"autoperiod", str::Str(remote_price.periods.at(
                            item_query->AsInt("period") / 12))}});

    sbin::ClientQuery("func=service.postprolong&sok=ok&elid=" + str::Str(iid));
    SyncItem(iid);
  }

  void Suspend(const int iid) override {
    // TODO: remove NS?
    sbin::ClientQuery("func=service.postsuspend&sok=ok&elid=" + str::Str(iid));
  }

  void Resume(const int iid) override {
    // TODO: restore NS?
    sbin::ClientQuery("func=service.postresume&sok=ok&elid=" + str::Str(iid));
  }

  void Close(const int iid) override {
    sbin::ClientQuery("func=service.postclose&sok=ok&elid=" + str::Str(iid));
  }

  void SyncItem(const int iid) override {
    Debug("Func: SyncItem");
    auto item_query = ItemQuery(iid);
    SetModule(item_query->AsInt("processingmodule"));

    StringMap item_params;
    AddItemParam(item_params, iid);

    int remote_status = -1;
    string expiredate;
    int status = -1;

    auto response = Remote_MakeRequest({{"func", "domain"}, {"api", "on"}});
    for (auto elem : response.elems()) {
      if (elem.FindNode("id").Str() == item_params[PARAM_REMOTE_ID]) {
        remote_status = str::Int(elem.FindNode("domainstatus").Str());
        expiredate = elem.FindNode("expire").Str();
      }
    }
    if (remote_status == -1 || remote_status == 0) {
      throw mgr_err::Missed("remote_domain", item_params[PARAM_REMOTE_ID]);
    } else if (remote_status == 2) {
      status = domain_util::isDelegated;
    } else if (remote_status == 3) {
      status = domain_util::isNoDelegated;
    }

    if (status != -1) {
      mgr_date::Date check(expiredate);  // Will throw if date is bad.
      sbin::ClientQuery("func=service.setstatus&elid=" + str::Str(iid) +
                        "&service_status=" + str::Str(status));
      sbin::ClientQuery("func=service.setexpiredate&elid=" + str::Str(iid) +
                        "&expiredate=" + str::url::Encode(expiredate));
    }
  }

  void UpdateNS(const int iid) override {
    Debug("Func: UpdateNS");

    auto item_query = ItemQuery(iid);
    SetModule(item_query->AsInt("processingmodule"));

    StringMap item_params;
    AddItemParam(item_params, iid);

    StringVector ns = GetNsVector(item_params);

    StringMap request{
        {"func", "domain.edit"}, {"sok", "ok"}, {"changens", "on"}};
    request["elid"] = item_params.at(PARAM_REMOTE_ID);
    int ns_id = 1;
    for (const auto& i : GetNsVector(item_params)) {
      request["ns" + str::Str(ns_id)] = i;
      ++ns_id;
    }
    Remote_MakeRequest(request);
  }

  void TuneConnection(mgr_xml::Xml& module_xml) override {
    auto slist =
        module_xml.GetRoot().AppendChild("slist").SetProp("name", "registrar");
    slist.AppendChild("msg", "registrar_0").SetProp("key", "-1");
    // g_enabled_registrars is filled in ctor.
    for (const auto& i : g_enabled_registrars) {
      slist.AppendChild("msg", "registrar_" + str::Str(i))
          .SetProp("key", str::Str(i));
    }
  }

  int ImportRemoteContact(int module, const string& remote_id) {
    auto remote = Remote_MakeRequest(
        {{"func", "domaincontact.edit"}, {"elid", remote_id}});
    StringMap local {{"type", "owner"}, {"sok", "ok"}, {"module", str::Str(module)}};
    auto copy = [&remote, &local](const string& dst, const string& src = "") {
      local[dst] = remote.value(!dst.empty() ? dst : src);
    };

    string remote_type = remote.value("ctype");

    copy("email");
    copy("phone");
    copy("fax");

    local["location_country"] = GetCountryId(GetRemoteCountryIso2(remote.value("la_country")));
    copy("location_state", "la_state");
    copy("location_postcode", "la_postcode");
    copy("location_city", "la_city");
    copy("location_address", "la_address");

    // Common fields for all russian types: postal address, mobile phone
    if (remote_type != "generic") {
      local["postal_country"] = GetCountryId(GetRemoteCountryIso2(remote.value("pa_country")));
      copy("postal_state", "pa_state");
      copy("postal_postcode", "pa_postcode");
      copy("postal_city", "pa_city");
      copy("postal_address", "pa_address");
      copy("postal_addressee", "pa_addressee");
      copy("mobile");
    }

    if (remote_type == "person") {
      copy("firstname_locale", "firstname_ru");
      copy("middlename_locale", "middlename_ru");
      copy("lastname_locale", "lastname_ru");
      copy("firstname");
      copy("middlename");
      copy("lastname");
      if (remote.value("inn") == "") {
        local["profiletype"] = str::Str(table::Profile::prPersonal);
      } else {
        local["profiletype"] = str::Str(table::Profile::prSoleProprietor);
        copy("inn");
      }
      copy("birthdate");
      copy("passport_series", "passport");
      copy("passport_org");
      copy("passport_date");
    } else if (remote_type == "company") {
      copy("company");
      copy("company_locale", "company_ru");
      copy("inn");
      copy("kpp");
      copy("ogrn");
      local["profiletype"] = str::Str(table::Profile::prCompany);
    } else if (remote_type == "generic") {
      if (remote.value("company") == "" || remote.value("company") == "N/A") {
        local["profiletype"] = str::Str(table::Profile::prPersonal);
      } else {
        local["profiletype"] = str::Str(table::Profile::prCompany);
        copy("company");
      }
      copy("firstname");
      copy("lastname");
    }
    int local_id = str::Int(sbin::ClientQuery("processing.import.profile", local).value("profile_id"));
    SetRemoteContactId(local_id, module, remote_type == "generic", remote_id);
    return local_id;
  }

  virtual void Import(const int module, const string& itemtype,
                      const string& search) {
    Debug("itemtype: %s, search: %s", itemtype.c_str(), search.c_str());

    SetModule(module);

    if (itemtype != "domain") {
      throw mgr_err::Error("not_implemented");
    }

    std::set<string> search_list;
    str::Split(search, " ", search_list);

    auto domains = Remote_MakeRequest({{"func", "domain"}, {"api", "on"}});

    for (auto i : domains.elems()) {
      string domain_name = i.FindNode("name").Str();
      if (!search_list.empty() && !search_list.count(domain_name)) continue;
      if (allowed_registrar_ != -1 &&
          str::Int(i.FindNode("registrarId")) != allowed_registrar_)
        continue;
      string remote_id = i.FindNode("id").Str();
      string tld_name = domain_name;
      str::GetWord(tld_name, ".");
      string tld_id = sbin::DB()
                          ->Query("SELECT id FROM tld WHERE name = " +
                                  sbin::DB()->EscapeValue(tld_name))
                          ->Str();
      mgr_date::Date expiredate;
      try {
        expiredate = mgr_date::Date(i.FindNode("expire").Str());
      } catch (...) {
      }

      auto domain_edit =
          Remote_MakeRequest({{"func", "domain.edit"}, {"elid", remote_id}});

      int domain_id = str::Int(
          sbin::ClientQuery(
              "processing.import.service",
              {
                  {"sok", "ok"},
                  {IMPORT_ITEMTYPE_INTNAME, itemtype},
                  {IMPORT_SERVICE_NAME, domain_name},
                  {"domain", domain_name},
                  {IMPORT_PRICELIST_INTNAME, tld_id},
                  {"status", (expiredate > mgr_date::Date() ? "2" : "3")},
                  {"period", "12"},
                  {"module", str::Str(module)},
                  {"expiredate", expiredate},
                  {"ns0", domain_edit.value("ns0")},
                  {"ns1", domain_edit.value("ns1")},
                  {"ns2", domain_edit.value("ns2")},
                  {"ns3", domain_edit.value("ns3")},
                  {PARAM_REMOTE_ID, remote_id},
                  // TODO: add PARAM_REMOTE_PRICE
              })
              .value("service_id"));
      if (domain_id == 0) throw mgr_err::Error("domain_import");

      std::set<string> required_contacts =
          std::set<string>(RUSSIAN_ZONES).count(tld_name)
              ? std::set<string>({"owner"})
              : std::set<string>({"owner", "admin", "bill", "tech"});

      for (const auto& contact_type : required_contacts) {
        string remote_contact_id = domain_edit.value(contact_type);
        if (remote_contact_id.empty()) {
          throw mgr_err::Missed("contact_" + contact_type);
        }
        int local_contact_id = GetLocalContactId(module, remote_contact_id);
        if (local_contact_id == -1) {
          local_contact_id = ImportRemoteContact(module, remote_contact_id);
        }
        sbin::ClientQuery("service_profile2item.edit",
                          {{"sok", "ok"},
                           {"service_profile", str::Str(local_contact_id)},
                           {"item", str::Str(domain_id)},
                           {"type", contact_type}});
      }
    }
  }
};
}  // namespace

RUN_MODULE(CLASS_NAME)
