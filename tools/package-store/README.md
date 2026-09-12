# DemiEngine Package Store

Symfony 7.4 LTS + Twig, served at https://demiengine.de.
The engine homepage is at / and the searchable catalog is at /packages/.
A responsive, free-only catalog and the existing engine's HTTP package registry.
Catalog presentation is stored separately from immutable engine manifests.

## Develop

Requires PHP >=8.2, Composer, XML, ctype and iconv. No database or Node build is
required. Filesystem releases are staged and atomically renamed after validation.

    composer install
    php bin/console about
    php tests/run.php
    php -S 127.0.0.1:8080 -t public public/router.php

Or from the engine root:

    docker build -t demi-store-dev tools/package-store
    docker run --rm -p 127.0.0.1:8086:8080 -v "$PWD/tools/package-store:/app" demi-store-dev

Install Composer dependencies before starting the container. The PHP development
server is only for local review; production uses the supplied Nginx/FPM configs.

## Listing metadata

All new listings require a title, description, publisher, license, category,
kind and format_version. Price must be zero. Assets support a cover image and up
to seven more previews. No images uses /images/demi-asset.svg. Broken remote
images also fall back to that logo in the browser.

    {
      "format_version": 1,
      "title": "My environment pack",
      "description": "What this contains and how to use it.",
      "publisher": "Author name",
      "license": "CC0-1.0",
      "kind": "asset",
      "category": "3D",
      "price": 0,
      "images": ["cover.png", "https://example.org/second-preview.jpg"],
      "tags": ["3d", "environment", "modular"]
    }

Local image paths are relative to the metadata file. Only PNG, JPEG and WebP
are accepted (10 MB / 40 megapixels each). HTTPS image URLs are linked directly;
the server does not fetch arbitrary remote images. List your cover first.

Tags are an optional list of up to 12 unique lowercase labels (letters, digits,
hyphens; maximum 40 characters). They are searchable, filterable, and determine
related packages. Title/description and tags can be updated without replacing
archives: php bin/console store:update-listing package.name metadata.json.
This command preserves existing images, archive bytes, hashes and release dates.

License IDs reference the shared Licenses service and licenses/*.txt pool.
BSD-3-Clause, MIT, Apache-2.0 and CC0-1.0 are included from SPDX license-list-data
v3.27.0. Add a canonical license text and an entry in Licenses::NAMES to support
another ID. Unknown IDs are rejected during import; license text is rendered
locally at /licenses/{id}. The supplied texts are standard templates; publishers
retain their package-specific copyright notices inside the package.

Package sections are server-rendered links, so keyboard navigation and direct
links work without JavaScript. Package Content lists manifest paths only and
never reads or renders file payloads. Releases lists downloads/checksums, and
Publisher info displays the listing's publisher. Reviews are not implemented.

## Publish

Publishing is currently restricted to maintainers with SSH access; no anonymous
upload or unauthenticated PUT/reset endpoint is exposed.

1. Build a .demipkg using the engine's local registry publish command:
   demi package publish path/to/package --registry /path/to/staging-registry
2. Transfer the resulting package.demipkg, metadata JSON and local preview images
   to the server.
3. Run the store console with access to the persistent data directory:
   DEMI_STORE_DATA=/var/lib/demi-store/catalog php bin/console store:import package.demipkg metadata.json

The import command checks identity, archive file checksums, paths, file sizes,
declared contents and trailing data before publication. Existing versions cannot
be overwritten. No uploaded code is executed or extracted into the web root.

For the repository's initial code packages, bin/export-seed.sh uses the engine
CLI and writes archives under var/seed. Listing metadata lives in catalog/.
These packages are BSD-3-Clause under the engine license. No downloaded
third-party example models are included in the public catalog.

## API and engine installation

    cd path/to/game
    demi package add demi.gameplay.health@1.0.0

Current DemiEngine uses ./demi.project.json and https://demiengine.de by default.
For custom registries, pass --registry explicitly. An explicit project
registry or DEMI_PACKAGE_REGISTRY still overrides the hosted default.

GET /v1/catalog exposes listing metadata, including linked images.
GET /v1/packages/{name} lists immutable releases using the engine protocol.
GET /v1/packages/{name}/{version}/archive downloads the checksum-verified archive.
GET /health is a simple health check.
The browser offers search, kind/category filters, alphabetical/recent sorting,
pagination, release history, image gallery and a copyable CLI command.

## Production layout

- /srv/demi-store/releases/<release>: application and Composer vendor dependencies.
- /srv/demi-store/current: symlink to the active release.
- /var/lib/demi-store/catalog: persistent releases, images and archives.
- Only public/ is the Nginx document root; raw release records stay outside it.
- deploy/nginx.conf and deploy/fpm-pool.conf define the dedicated virtual host/pool.
- APP_ENV=prod; debugging and display_errors disabled.
- Nginx rate-limits dynamic requests. PHP-FPM workers run as www-data.
- TLS is provisioned with Certbot for demiengine.de after its A record resolves.

Before switching current, install locked dependencies, warm the Symfony production
cache, run checks, and validate nginx -t. Keep prior releases for rollback.
Back up /var/lib/demi-store/catalog separately; it is not part of the code release.
Anonymous browsing requires no cookies, account, or checkout.

## Verification

`composer test` covers metadata, archive integrity, immutable publishing, fallback
images, local/remote gallery entries and HTTP routes. `tests/browser.cjs` uses
Playwright to check search, filtering, downloads, empty states and mobile overflow.
Set STORE_URL to test the deployed site; PLAYWRIGHT_MODULE may point to a local
Playwright installation. The browser tests do not mutate the live catalog.

Initial deployment: 2026-09-12, server 91.218.66.127, PHP 8.5 FPM and Nginx.
Let's Encrypt certificate renewal is managed by certbot.timer. The checked-in
Nginx file is the HTTP bootstrap; Certbot adds the production TLS directives.
Do not overwrite that live TLS configuration with the bootstrap on later releases.
