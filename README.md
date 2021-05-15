# tinysrv

Tinysrv is small single process webserver with support for TLS that responds with empty content of proper content-type if requested file is not found.

### usage
Start tinysrv in **f**oreground on **p**ort 8080.
```
./tinysrv -f -p 8080
```

### development

```
apt-get update
apt-get install -y gcc libc-dev libssl-dev make
```