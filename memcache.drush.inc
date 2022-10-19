<?php
/**
 * @file
 * Provides drush commands for Memcached.
 */

// Drush commands only wrap around the bee equivalents.
include_once __DIR__ . '/memcache.bee.inc';

/**
 * Implements hook_drush_command().
 */
function memcache_drush_command() {
  $commands = memcache_bee_command();
  foreach ($commands as &$command) {
    $command['callback'] = str_replace('bee_', 'drush_', $command['callback']);
  }
  return $commands;
}

/**
 * Display memcache statistics.
 *
 * @param string $bin
 *   The bin to retrieve statistics for.
 * @param string $stats_type
 *   The type of statistics to retrieve when using the Memcache extension.
 */
function memcache_drush_stats($bin = 'cache', $stats_type = 'default') {
  $arguments = [
    'bin' => $bin,
    'stats_type' => $stats_type,
  ];
  $options = [
    'interactive' => drush_get_option('interactive', 0),
    'aggregate' => drush_get_option('aggregate', 0),
  ];

  memcache_bee_stats($arguments, $options);
}

/**
 * Invalidate all items in specified bin.
 *
 * @param string $bin
 *   The bin to flush. Note that this will flush all bins mapped to the same
 *   server as $bin. There is no way at this time to empty just one bin.
 *
 * @return bool
 *   Returns TRUE on success or FALSE on failure.
 */
function memcache_drush_flush($bin = 'cache') {
  return memcache_bee_flush(['bin' => $bin]);
}

/**
 * Implements drush_hook_COMMAND_validate().
 */
function drush_memcache_flush_validate() {
  return _memcache_cli_is_available();
}

/**
 * Implements drush_hook_COMMAND_validate().
 */
function drush_memcached_stats_validate() {
  return _memcache_cli_is_available();
}
