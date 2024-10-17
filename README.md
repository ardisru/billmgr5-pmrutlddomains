# billmgr5-pmrutlddomains

Модуль обработки услуг для интеграции BILLmanager 5/6 с сервисом регистрации доменов [RU-TLD](https://ru-tld.ru). Лицензия для установки модуля не требуется. 

## Инструкции по установке для Centos 7 / Almalinux 9

Перейдите в директорию */usr/local/mgr5/src/* и выполните следующие команды от имени пользователя *root*:

```sh
# Перед первой компиляцией нужно установить сборочные зависимости и пакет с заголовочными файлами API
yum install billmanager-devel
make -f isp.mk centos-prepare
# Устанавливаем модуль
git clone --recursive https://github.com/ardisru/billmgr5-pmrutlddomains.git pmrutlddomains
make -C pmrutlddomains all install
```

## Инструкции по установке для Ubuntu

Перейдите в директорию */usr/local/mgr5/src/* и выполните следующие команды от имени пользователя *root*:

```sh
# Перед первой компиляцией нужно установить сборочные зависимости и пакет с заголовочными файлами API
apt-get install billmanager-devel
make -f isp.mk debian-prepare
# Устанавливаем модуль
git clone --recursive https://github.com/ardisru/billmgr5-pmrutlddomains.git pmrutlddomains
make -C pmrutlddomains all install
```
