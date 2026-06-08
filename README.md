# tinysrv

Tinysrv is a small single process webserver with support for TLS that responds with empty content of proper content-type if requested file is not found.

### usage
Start tinysrv in **f**oreground on **p**ort 8080.
```
./tinysrv -f -p 8080
```

Start tinysrv in background, keep it running under user tinysrv, write PID of controlling process to `/var/run/tinysrv/tinysrv.pid`, bind HTTPS to IP 10.0.0.2 port 443 and serve files from `/var/www/` directory over HTTP from IP 10.0.0.3 port 80.
```
./tinysrv -u tinysrv -P /var/run/tinysrv/tinysrv.pid -k 443 10.0.0.2 -p 80 -S /var/www/ 10.0.0.3
```

### development
```
apt-get update
apt-get install -y gcc libc-dev libssl-dev make
```

### compiling
`make` or `make USE_SSL=1`