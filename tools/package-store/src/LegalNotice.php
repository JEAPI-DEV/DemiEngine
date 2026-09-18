<?php
namespace App;

/** Contact data belongs to server configuration, never application releases. */
final class LegalNotice
{
    public function load(): array
    {
        $path = getenv('DEMI_IMPRESSUM_FILE') ?: '/etc/demi-store/impressum.json';
        $raw = @file_get_contents($path, false, null, 0, 16385);
        if ($raw === false || strlen($raw) > 16384) {
            throw new \RuntimeException('Legal notice configuration unavailable.');
        }
        $value = json_decode($raw, true, 32, JSON_THROW_ON_ERROR);
        if (!is_array($value) || ($value['format_version'] ?? null) !== 1) {
            throw new \RuntimeException('Invalid legal notice configuration.');
        }
        foreach (['name', 'phone', 'email'] as $key) {
            if (!is_string($value[$key] ?? null) || trim($value[$key]) === '' || strlen($value[$key]) > 240) {
                throw new \RuntimeException('Incomplete legal notice configuration.');
            }
        }
        $address = $value['address'] ?? null;
        if (!is_array($address) || !array_is_list($address) || count($address) < 1 || count($address) > 6) {
            throw new \RuntimeException('Invalid legal notice address.');
        }
        foreach ($address as $line) {
            if (!is_string($line) || trim($line) === '' || strlen($line) > 240) {
                throw new \RuntimeException('Invalid legal notice address.');
            }
        }
        if (!filter_var($value['email'], FILTER_VALIDATE_EMAIL) ||
            !preg_match('/^\+?[0-9 ()\/.\-]{5,40}$/D', $value['phone'])) {
            throw new \RuntimeException('Invalid legal notice contact information.');
        }
        return [
            'name' => $value['name'], 'address' => $address,
            'phone' => $value['phone'], 'email' => $value['email'],
            'phone_uri' => preg_replace('/[^+0-9]/', '', $value['phone']),
        ];
    }
}
