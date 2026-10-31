<?php

/**
 * @file
 * Uses the Memcached container as the default cache storage in CI.
 */

$settings['cache_backends'][] = 'modules/memcache/memcache.inc';
$settings['cache_default_class'] = 'BackdropMemcache';
$settings['lock_inc'] = 'modules/memcache/memcache-lock.inc';
$settings['memcache_stampede_protection'] = TRUE;
$settings['memcache_servers'] = ['memcached:11211' => 'default'];
