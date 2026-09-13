FROM composer:2 AS composer
FROM php:8.4-cli
RUN apt-get update && apt-get install -y --no-install-recommends git unzip libxml2-dev && docker-php-ext-install xml && rm -rf /var/lib/apt/lists/*
COPY --from=composer /usr/bin/composer /usr/local/bin/composer
WORKDIR /app
ENV APP_ENV=dev APP_DEBUG=0
CMD ["php", "-S", "0.0.0.0:8080", "-t", "public", "public/router.php"]

