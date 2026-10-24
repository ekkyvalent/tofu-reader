# Local firmware download page

`web/download/index.html` is a static page for local Xteink X4 Pro firmware builds.
`scripts/publish-local.sh` copies artifacts from `.pio/build/<env>/` and writes
`manifest.json`; the page reads that manifest in the browser.

## Publishing

```sh
scripts/publish-local.sh                 # env x4pro, keep 5 versions
scripts/publish-local.sh x4pro --keep 3
```

The destination root defaults to `/srv/tofu-reader`; override it with
`TOFU_DOWNLOAD_ROOT`. The script writes `www/index.html`, `www/manifest.json`, and the
binaries under `dl/`. It does not install or reload nginx and does not use sudo.

## Serving

The site is bound to the tailnet address only: `http://100.124.226.84:8099/`.
It is not indexed and is not reachable from the public internet.

Reference nginx server block (the operator installs this; the publish script does not):

```nginx
server {
    listen 100.124.226.84:8099;
    server_name _;
    root /srv/tofu-reader/www;
    index index.html;

    add_header X-Robots-Tag "noindex, nofollow" always;

    location /dl/ {
        alias /srv/tofu-reader/dl/;
    }
}
```
