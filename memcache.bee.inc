<?php
/**
 * @file
 * Provides bee commands for Memcached.
 */

/**
 * Implements hook_bee_command().
 */
function memcache_bee_command() {
  $is_drush = function_exists('drush_env');
  $t = $is_drush ? 'dt' : 'bt';

  $items['memcache-stats'] = array(
    'description' => $t('Retrieve statistics from memcache.'),
    'optional_arguments' => array('bin', 'type'),
    'arguments' => array(
      'bin' => $t('Optionally specify which bin; defaults to "cache".'),
      'type' => $t('Optionally specify type of statistics; one of {default, reset, malloc, slabs, cachedump, items, sizes}.'),
    ),
    'options' => array(
      'interactive' => $t('Interactivly specify which type of statistics to display.'),
      'aggregate' => $t('Included combined statistics from all servers.'),
    ),
    'required-arguments' => 0,
    'examples' => array(
      'memcache-stats' => 'Display raw statistics.',
      'memcache-stats --aggregate' => 'Display raw statistics for all servers and combined totals',
      'memcache-stats --interactive' => 'Interactively select which type of statistics to display.',
      'memcache-stats cache slabs' => 'Display slabs allocated for cache bin.',
    ),
    'aliases' => array('mcs'),
    'callback' => 'memcache_bee_stats',
    'bootstrap' => 1, // Page cache bootstrap.
  );

  $items['memcache-flush'] = array(
    'description' => $t('Flush all objects from a bin.'),
    'optional_arguments' => array('bin'),
    'arguments' => array(
      'bin' => $t('Optionally specify which bin to flush; defaults to "cache".'),
    ),
    'required-arguments' => 0,
    'examples' => array(
      'memcache-clear' => 'Flush all items in the default "cache" bin.',
      'memcache-clear page' => 'Flush all items in the "page" bin.',
    ),
    'aliases' => array('mcf'),
    'callback' => 'memcache_bee_flush',
    'bootstrap' => 1, // Page cache bootstrap.
  );

  return $items;
}

/**
 * Display memcache statistics.
 *
 * @param array $arguments
 *   An array of arguments specified in memcache_bee_commands().
 * @param array $options
 *   An array of options specified in memcache_bee_commands().
 */
function memcache_bee_stats($arguments = array(), $options = array()) {
  $is_drush = function_exists('drush_env');
  $choice = $is_drush ? 'drush_choice' : 'bee_choice';
  $t = $is_drush ? 'dt' : 'bt';

  // Convert arguments and options.
  $bin = !empty($arguments['bin']) ? $arguments['bin'] : 'cache';
  $stats_type = !empty($arguments['type']) ? $arguments['type'] : 'default';
  $interactive = isset($options['interactive']) ? $options['interactive'] : 0;
  $aggregate = isset($options['aggregate']) ? $options['aggregate'] : 0;

  // Shorten up bin name. Backdrop discards the "cache_" prefix if set.
  $bin = str_replace('cache_', '', $bin);

  if (_memcache_cli_is_available()) {
    if ($interactive) {
      $options = array(
        'default' => 'default',
        'reset' => 'reset',
        'malloc' => 'malloc',
        'slabs' => 'slabs',
        'cachedump' => 'cachedump',
        'items' => 'items',
        'sizes' => 'sizes',
      );
      $stats_type = $choice($options, $t('What type of statistics would you like to see?'), 'default');
      // stats_type of FALSE === Cancel
      if ($stats_type === FALSE) {
        return;
      }
    }
    $stats = dmemcache_stats($bin, $stats_type, $aggregate);

    if ($is_drush) {
      drush_print_r($stats);
    }
    else {
      bee_message(print_r($stats, 1));
    }
  }
}

/**
 * Invalidate all items in specified bin.
 *
 * @param array $arguments
 *   An array of arguments specified in memcache_bee_commands().
 * @param array $options
 *   An array of options specified in memcache_bee_commands().
 *
 * @return bool
 *   Returns TRUE on success or FALSE on failure.
 */
function memcache_bee_flush($arguments = array(), $options = array()) {
  $bin = $arguments['bin'];
  if (empty($bin)) {
    $bin = 'cache';
  }

  $is_drush = function_exists('drush_env');
  $t = $is_drush ? 'dt' : 'bt';
  $message = $is_drush ? 'drush_log' : 'bee_message';
  $success = $is_drush ? 'ok' : 'success';

  // Shorten up bin name. Backdrop discards the "cache_" prefix if set.
  $bin = str_replace('cache_', '', $bin);

  if (_memcache_cli_is_available()) {
    $flushed = dmemcache_flush($bin);

    if ($flushed === TRUE) {
      $message($t('Successfully cleared !bin bin.', array('!bin' => $bin)), $success);
      return TRUE;
    }
    else {
      $message($t('Failed to clear !bin bin.', array('!bin' => $bin)), 'error');
      return FALSE;
    }
  }
  return FALSE;
}

/**
 * Check if memcache.inc has been included.
 */
function _memcache_cli_is_available() {
  $is_drush = function_exists('drush_env');
  $message = $is_drush ? 'drush_log' : 'bee_message';

  if (!class_exists('BackdropMemcache', FALSE)) {
    $message('BackdropMemcache class is not available', 'Please review README.md to check your site configuration.', 'error');
    return FALSE;
  }
  return TRUE;
}
