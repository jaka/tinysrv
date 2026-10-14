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

Serve only real files, without empty placeholder responses, so a missing file returns `404` instead of the default empty content (use **F**):
```
./tinysrv -f -F -p 8080 -S /var/www/
```

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

Provide the addresses and options through the environment file referenced by the unit, e.g. `/run/tinysrv/tinysrv.args`:
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
