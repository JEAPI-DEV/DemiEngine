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
It applies the listing details to all existing versions. Browser editing below
targets the latest version shown in the storefront instead.

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

Maintainers publish at https://demiengine.de/publishing using a private publishing
key. Load the key file or paste its contents, unlock the form, then upload the
archive and listing details. The browser keeps the key only in memory and clears
it after publication or when locked. Visitors without a valid key cannot upload.

To change an existing listing, unlock the page, choose **Edit listing**, and
select the package. Its current details are filled in automatically. Edit the
title, description, publisher, license label, type, category or tags; keep or
remove existing previews and upload replacements. Saving updates the listing
without uploading an archive. An empty gallery uses the DemiEngine Asset logo.
Changing the license label does not change license files inside the archive.
Package identity, version, manifest, archive hash/bytes and release date remain
unchanged. Concurrent edits are rejected until the listing is reloaded. Old
preview files remain available for previously loaded pages; they are not deleted.

1. Build a .demipkg using the engine's local registry publish command:
   demi package publish path/to/package --registry /path/to/staging-registry
2. Upload the resulting archive through the publishing page, or use the uploader:
   python3 tools/package-store/bin/publish.py package.demipkg metadata.json

The uploader reads ~/.config/demi-store/publish-token by default (override with
--key-file or DEMI_PUBLISH_KEY), and uses https://demiengine.de (override with
--registry). Local preview paths in metadata are relative to the metadata file.
The key is sent over HTTPS in an Authorization header, not in command arguments.
The server-side store:import console command remains available for operators.

Operators store only the SHA-256 digest of the 64-character lowercase hex key
(without its trailing newline) in /etc/demi-store/publish-token.sha256, readable
by www-data but not public. DEMI_PUBLISH_KEY_FILE overrides this location;
DEMI_PUBLISH_TOKEN_HASH can instead supply the digest through the environment.
Replace the digest to revoke the old key. Keep the actual key outside the repo.

POST /api/publishing/check verifies a Bearer key. POST /api/publishing/releases
accepts authenticated multipart fields archive, metadata (JSON), and optional
images[] whose filenames match metadata image entries. Responses are 201 for
publication, 401 for unauthorized access, 409 for an existing version, and 422
for invalid input. Publishing without a configured key is disabled.

GET /api/publishing/listings/{name} requires the same Bearer key and returns
the latest record plus its revision token. POST /api/publishing/listings accepts
name, version, revision, metadata (complete JSON listing), and optional images[].
Only listing fields are accepted; archive uploads are rejected. Existing image
references may be retained only from that release. Successful edits return 200;
a stale revision returns 409 and a missing package/version returns 404. This
endpoint uses the same Nginx authentication and upload limits as new releases.

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
- The catalog directories must be writable by www-data; application source must
  remain read-only. Uploads allow a 512 MiB archive plus previews (600 MiB request
  limit), with five publishing requests per minute and a burst of three. Nginx
  checks authentication before passing the upload body to PHP.
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
