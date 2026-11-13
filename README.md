# tinysrv

Tinysrv is a small single process webserver with support for TLS that responds with empty content of proper content-type if requested file is not found. It handles up to 10 connections at once without blocking, so a slow client does not hold up the others (`MAX_CONNECTIONS` in `src/project.h`).

### usage
Start tinysrv in **f**oreground on **p**ort 8080.
```
./tinysrv -f -p 8080
```

Start tinysrv in background, keep it running under user tinysrv, write PID of controlling process to `/var/run/tinysrv/tinysrv.pid`, bind HTTPS to IP 10.0.0.2 port 443 and serve files from `/var/www/` directory over HTTP from IP 10.0.0.3 port 80.
```
./tinysrv -u tinysrv -P /var/run/tinysrv/tinysrv.pid -k 443 10.0.0.2 -p 80 -S /var/www/ 10.0.0.3
```

Serve only real files, without empty placeholder responses, so a missing file returns `404` instead of the default empty content (use **F**):
```
./tinysrv -f -F -p 8080 -S /var/www/
```

### TLS certificates
Serve HTTPS on port 443 with certificates from `/etc/ssl-deploy` (build with `make USE_SSL=1`):
```
./tinysrv -k 443 -C /etc/ssl-deploy -S /var/www 10.0.0.2
```

For every connection, tinysrv picks the certificate by the host name the client asks for (SNI). The certificate directory holds one file per host name. The file is named after the host name in lower case, with dots replaced by `_` and no extension, and contains the certificate (with its chain) followed by the private key. A wildcard certificate replaces the first label with `+`:

| host name | certificate file |
|---|---|
| `www.example.com` | `www_example_com` |
| `*.example.com` | `+_example_com` |

Create it from a Let's Encrypt certificate:
```
sudo sh -c 'umask 077; cat /etc/letsencrypt/live/www.example.com/fullchain.pem \
  /etc/letsencrypt/live/www.example.com/privkey.pem > /etc/ssl-deploy/www_example_com'
sudo chown tinysrv /etc/ssl-deploy/www_example_com
```
The file is read on every TLS handshake by the user given with `-u`, so that user must be able to read it, and a renewed certificate is used without a restart. Repeat the `cat` after every renewal, e.g. from a certbot deploy hook in `/etc/letsencrypt/renewal-hooks/deploy/`.

### development
```
apt-get update
apt-get install -y gcc libc-dev libssl-dev make
```

### compiling
`make` or `make USE_SSL=1`

### installing
Build and install the binary and the reference systemd unit.
```
make
sudo make install
```
This installs `/usr/local/bin/tinysrv` and `/usr/local/lib/systemd/system/tinysrv.service`. Override the locations with `PREFIX=` and `DESTDIR=` (and remove them again with `sudo make uninstall`).

Provide the addresses and options through the environment file referenced by the unit, e.g. `/etc/default/tinysrv.args`:
```
ARGS=-p 80 -S /var/www/ 0.0.0.0
```
Then enable and start the service.
```
sudo systemctl daemon-reload
sudo systemctl enable --now tinysrv
```

If tinysrv must bind a privileged port (below 1024) while running as `User=tinysrv`, the unit grants `CAP_NET_BIND_SERVICE` through `AmbientCapabilities=`. When starting tinysrv yourself instead, grant the capability to the binary:
```
sudo setcap cap_net_bind_service=+ep /usr/local/bin/tinysrv
```
The capability is dropped whenever the file is replaced (copy/strip), so re-apply it after every install.
