# Running a local map tile cache (e.g. for MidCoast NSW)

Map downloads on the T-Deck are slow mostly because every tile is a round trip to a tile server
overseas. A caching proxy in Australia makes them several times faster, and tiles one person has
fetched come straight from the cache for the next person.

**Only map tiles go through it** (and, later, EasyDeck update files, which are checked against the
SHA-256 published on GitHub, so a mirror can't tamper with them). It is not a general web proxy.

## nginx example

```nginx
proxy_cache_path /var/cache/nginx/tiles levels=1:2 keys_zone=tiles:50m max_size=30g inactive=180d use_temp_path=off;

server {
    listen 443 ssl;
    server_name tiles.example.org;          # your host
    # ssl_certificate / ssl_certificate_key ...

    # /outdoors/14/15123/9734.png -> upstream tile (only z/x/y PNG paths are allowed)
    location ~ "^/(outdoors|landscape)/([0-9]{1,2})/([0-9]{1,6})/([0-9]{1,6})\.png$" {
        proxy_pass https://tile.thunderforest.com/$1/$2/$3/$4.png?apikey=YOUR_KEY;
        proxy_ssl_server_name on;
        proxy_cache tiles;
        proxy_cache_valid 200 60d;
        proxy_cache_lock on;                 # many T-Decks asking for the same tile = one upstream request
        proxy_ignore_headers Cache-Control Expires Set-Cookie;
        add_header X-Cache $upstream_cache_status;
        limit_except GET { deny all; }
    }
    location / { return 404; }
}
```

On the T-Deck: *Offline maps → Map source → Custom URL* →
`https://tiles.example.org/outdoors/{z}/{x}/{y}.png` (or put `map_url=` in a settings pack).

**Planned (nah.rocks):** instead of one shared key, the T-Deck sends the user's own API key with
each request; the mirror fetches the tile with that key, caches it and returns it over HTTPS.
EasyDeck first checks that the mirror is up and falls back to downloading directly if it isn't.
(The example above uses a single server-side key - check your provider's terms before sharing
one key between many users.)

## Faster still: download on a computer

`tools/get_tiles.py` downloads an area straight onto the SD card at computer speed - see the
top of that file for an example.
