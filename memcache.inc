<?php
/**
 * @file
 * Implementation of cache.inc with memcache logic included.
 */

require_once __DIR__ . '/dmemcache.inc';

/**
 * Defines the period after which wildcard clears are not considered valid.
 */
const MEMCACHE_WILDCARD_INVALIDATE = 2419200;

/**
 * Implementation of cache.inc with memcache logic included
 */
class BackdropMemcache implements BackdropCacheInterface {

  /**
   * @var Memcache|Memcached The raw PECL memcache(d) object for this bin.
   */
  protected $memcache;

  /**
   * @var string The cache bin name.
   */
  protected $bin;

  /**
   * @var array Array of cache key prefixes and when they were last invalidated.
   */
  protected $wildcard_flushes;

  /**
   * @var int Maximum length of time a wildcard flush timestamp is respected.
   */
  protected $wildcard_lifetime;

  /**
   * @var int Timestamp of the last full flush of this cache bin.
   */
  protected $flush_timestamp;

  /**
   * @var int Timestamp of the last garbage collection of this cache bin.
   */
  protected $garbage_collection_timestamp;

  /**
   * @var int
   */
  protected $flushed;

  /**
   * @var array A list of cache IDs that have been set this request.
   *
   * This variable is utilized to keep track of cache entries that are set
   * within the same REQUEST_TIME (the same second) and return them as valid
   * even when a cache flush has occurred within the same request.
   */
  protected $set_cids = [];

  /**
   * Constructs a BackdropMemcache object.
   *
   * @param string $bin
   *   The cache bin for which the object is created.
   */
  public function __construct($bin) {
    $this->memcache = dmemcache_object($bin);
    $this->bin = $bin;
    $this->reloadVariables();
  }

  /**
   * Implements BackdropCacheInterface::get().
   */
  public function get($cid) {
    $cache = dmemcache_get($cid, $this->bin, $this->memcache);
    $valid = $this->valid($cid, $cache);
    if ($valid) {
      return $cache;
    }
    else {
      return FALSE;
    }
  }

  /**
   * Implements BackdropCacheInterface::getMultiple().
   */
  public function getMultiple(array &$cids) {
    $results = dmemcache_get_multi($cids, $this->bin, $this->memcache);
    foreach ($results as $cid => &$result) {
      if (!$this->valid($cid, $result)) {
        // This object has expired, so don't return it.
        unset($results[$cid]);
      }
    }
    // Remove items from the referenced $cids array that we are returning,
    // per the comment in cache_get_multiple() in includes/cache.inc.
    $cids = array_diff($cids, array_keys($results));
    return $results;
  }

  /**
   * Checks if a retrieved cache item is valid.
   *
   * @param string $cid
   *   The cache id of the item
   * @param mixed $cache
   *   The cache item, which will be updated if needed.
   *
   * @return bool
   *   Whether the item is valid.
   */
  protected function valid($cid, &$cache) {
    if ($cache) {
      // Items that have expired are invalid. This includes CACHE_TEMPORARY
      // items that were set prior to the last garbage collection, and
      // timestamp set items.
      //
      // Test whether this cache has been garbage collected.
      $cache_garbage_collected = $cache->created <= $this->garbage_collection_timestamp;

      // Item is expired if the request time is greater than expire time.
      $cache_timestamp_expired = $cache->expire <= REQUEST_TIME;

      // The set_cids array is checked to ensure any cache IDs set after a flush
      // within this same request are valid.
      $set_this_request = isset($this->set_cids[$cache->cid]);

      // Items created before the last full flush against this bin are always
      // invalid.
      if (!$set_this_request && $cache->created <= $this->flush_timestamp) {
        $cache = FALSE;
      }
      // Test for expired items.
      elseif (!$set_this_request && ($cache->expire == CACHE_TEMPORARY && $cache_garbage_collected) || ($cache->expire > 0 && $cache_timestamp_expired)) {
        // If the memcache_stampede_protection variable is set, allow one
        // process to rebuild the cache entry while serving expired content to
        // the rest. Note that core happily returns expired cache items as valid
        // and relies on cron to delete them, but this is mostly reliant on its
        // use of CACHE_TEMPORARY which does not map well to memcache.
        if (settings_get('memcache_stampede_protection', FALSE) && $this->stampedeProtected($cid)) {
          // The process that acquires the lock will get a cache miss, all
          // others will get a cache hit.
          if ($this->lockInit() && lock_acquire("memcache_$cid:$this->bin", settings_get('memcache_stampede_semaphore', 15))) {
            $cache = FALSE;
          }
        }
        else {
          $cache = FALSE;
        }
      }
      // Finally, check for wildcard clears against this cid.
      else {
        if (!$this->wildcardValid($cid, $cache)) {
          $cache = FALSE;
        }
      }
    }

    // On cache misses, attempt to avoid stampedes when the
    // memcache_stampede_protection variable is enabled.
    if (!$cache) {
      if (settings_get('memcache_stampede_protection', FALSE) && $this->lockInit() && $this->stampedeProtected($cid) && !lock_acquire("memcache_$cid:$this->bin", settings_get('memcache_stampede_semaphore', 15))) {
        // Prevent any single request from waiting more than three times due to
        // stampede protection. By default this is a maximum total wait of 15
        // seconds. This accounts for two possibilities - a cache and lock miss
        // more than once for the same item. Or a cache and lock miss for
        // different items during the same request.
        // @todo: it would be better to base this on time waited rather than
        // number of waits, but the lock API does not currently provide this
        // information. Currently the limit will kick in for three waits of 25ms
        // or three waits of 5000ms.
        static $lock_count = 0;
        $lock_count++;
        if ($lock_count <= settings_get('memcache_stampede_wait_limit', 3)) {
          // The memcache_stampede_semaphore variable was used in previous
          // releases of memcache, but the max_wait variable was not, so by
          // default divide the semaphore value by 3 (5 seconds).
          lock_wait("memcache_$cid:$this->bin", settings_get('memcache_stampede_wait_time', 5));
          $cache = $this->get($cid);
        }
      }
    }

    $valid = (bool) $cache;

    if (settings_get('memcache_pagecache_header', FALSE) && ($this->bin == 'cache_page')) {
      // Per RFC-6648, don't start with X-
      $header = t('Backdrop-Pagecache-Memcache: !status', ['!status' => $valid ? t('HIT') : t('MISS')]);
      if ($valid) {
        $header .= t(', age=!age', ['!age' => REQUEST_TIME - $cache->created]);
      }
      header($header);
    }

    return $valid;
  }

  /**
   * Implements BackdropCacheInterface::set().
   */
  public function set($cid, $data, $expire = CACHE_PERMANENT) {
    $created_microtime = round(microtime(TRUE), 3);

    // Record this as being a freshly set cache ID so it's considered valid
    // even if a cache flush occurred within this same request.
    $this->set_cids[$cid] = $expire;

    // Create new cache object.
    $cache = new stdClass();
    $cache->cid = $cid;
    $cache->data = is_object($data) ? clone $data : $data;
    $cache->created = REQUEST_TIME;
    $cache->created_microtime = $created_microtime;
    $cache->expire = $expire;
    // Record the previous number of wildcard flushes affecting our cid.
    $cache->flushes = $this->wildcardFlushes($cid);

    // Determine stored memcache item lifetime.
    if ($expire == CACHE_PERMANENT) {
      // Set memcache item to never expire.
      $memcache_expire = 0;
    }
    elseif ($expire == CACHE_TEMPORARY) {
      // Temporary cache items are unclear when they need to be cleaned up, set
      // to 30 days.
      $memcache_expire = REQUEST_TIME + 2591999;
    }
    elseif (settings_get('memcache_stampede_protection', FALSE)) {
      // If stampede protection is enabled, set the item expiration to twice
      // its intended lifetime. The expired object may be served while one
      // process rebuilds it.
      $memcache_expire = REQUEST_TIME + (($expire - REQUEST_TIME) * 2);
    }
    else {
      // Allow the memcache item to expire at the exact absolute timestamp.
      $memcache_expire = $expire;
    }
    dmemcache_set($cid, $cache, $memcache_expire, $this->bin, $this->memcache);

    // Release lock if acquired in $this->valid().
    $lock = "memcache_$cid:$this->bin";
    if (settings_get('memcache_stampede_protection', FALSE) && isset($GLOBALS['locks'][$lock])) {
      lock_release("$lock");
    }
  }

  /**
   * Deletes an item from the cache.
   *
   * @param string $cid
   *    The cache ID to delete.
   */
  function delete($cid) {
    // Entity cache passes in an array instead of a single ID. This would be
    // fixed by removing core's use of cache_clear_all().
    // See https://github.com/backdrop/backdrop-issues/issues/2158
    // @todo Remove this when fixed in core.
    if (is_array($cid)) {
      $this->deleteMultiple($cid);
      return;
    }

    dmemcache_delete($cid, $this->bin, $this->memcache);
    if (isset($this->set_cids[$cid])) {
      unset($this->set_cids[$cid]);
    }
  }

  /**
   * Deletes multiple items from the cache.
   *
   * @param $cids
   *   An array of $cids to delete.
   */
  function deleteMultiple(array $cids) {
    foreach ($cids as $cid) {
      dmemcache_delete($cid, $this->bin, $this->memcache);
    }
  }

  /**
   * Deletes items from the cache using a wildcard prefix.
   *
   * @param string $prefix
   *   A wildcard prefix.
   */
  function deletePrefix($prefix) {
    // Register a wildcard flush for current cid.
    $this->wildcards($prefix, TRUE);
  }

  /**
   * Remove all items from a cache bin.
   *
   * In a SQL-based cache, this truncates the cache table.
   *
   * Memcache does have a flush_all command, but because a single Memcache
   * instance may have multiple cache bins stored in it, we cannot use it.
   *
   * Instead, store a timestamp of the last full bin flush, and any items
   * retrieved before that time are considered invalid and not returned.
   */
  function flush() {
    $this->flush_timestamp = REQUEST_TIME;
    $this->cacheStateSet('flush_timestamp', $this->bin, $this->flush_timestamp);

    // Remove the list of set cache IDs that occurred prior to this flush.
    $this->set_cids = [];

    // Remove any wildcard flushes as they are no longer relevant.
    if (isset($this->wildcard_flushes[$this->bin])) {
      // It is possible for multiple bins to be flushed during a single
      // page load, so cache the list in a static variable which is later
      // read in the flushBinsOnShutdown callback during shutdown.
      $bins_flushed = &backdrop_static('memcache_bins_flushed', []);
      if (empty($bins_flushed)) {
        // Only register the shutdown function once, on the first bin
        // flushed during this page load.
        register_shutdown_function(['BackdropMemcache', 'flushBinsOnShutdown']);
      }
      // Add to the list of bins that have been flushed.
      $bins_flushed[] = $this->bin;
    }
  }

  /**
   * Performs "garbage collection" on a cache bin.
   *
   * In the SQL-based cache, this deletes all temporary items.
   *
   * Memcache does not have the concept of cache lifetime. Instead of deleting
   * expired items, we save a timestamp of when temporary items are considered
   * invalid, and do not return those items in the future.
   */
  function garbageCollection() {
    // Update the timestamp of the last CACHE_TEMPORARY clear. All
    // temporary cache items created before this will be considered invalid.
    $this->garbage_collection_timestamp = REQUEST_TIME;
    $this->cacheStateSet('garbage_collection_timestamp', $this->bin, $this->garbage_collection_timestamp);
    // Remove any temporary items set this request.
    $this->set_cids = array_diff($this->set_cids, [CACHE_TEMPORARY]);
  }

  /**
   * Cleanup wildcard flushes on shutdown when one or more bins are flushed.
   */
  static function flushBinsOnShutdown() {
    // Get a fresh copy of the wildcard_flushes variable.
    $wildcard_flushes = BackdropMemcache::cacheStateGet('wildcard_flushes', NULL, []);
    // Load the static variable that was populated during clear().
    $bins_flushed = &backdrop_static('memcache_bins_flushed', []);
    // Step through each bin and fully remove it from wildcard_flushes.
    foreach ($bins_flushed as $bin) {
      if (isset($wildcard_flushes[$bin])) {
        unset($wildcard_flushes[$bin]);
      }
    }
    // Store the memcache_wildcard_flushes state without the flushed bins.
    BackdropMemcache::cacheStateSet('wildcard_flushes', NULL, $wildcard_flushes);
  }

  /**
   * Sum of all matching wildcards.
   *
   * Checking any single cache item's flush value against this single-value sum
   * tells us whether or not a new wildcard flush has affected the cached item.
   *
   * @param string $cid
   *   The cache id to check.
   *
   * @return int
   *   Sum of all matching wildcards for the given cache id.
   */
  protected function wildcardFlushes($cid) {
    return array_sum($this->wildcards($cid));
  }

  /**
   * Retrieves all matching wildcards for the given cache id.
   *
   * Utilize multiget to retrieve all possible wildcard matches, storing
   * statically so multiple cache requests for the same item on the same page
   * load doesn't add overhead.
   */
  protected function wildcards($cid, $flush = FALSE) {
    static $wildcards = [];
    $matching = [];

    if (!is_string($cid) && !is_int($cid)) {
      register_shutdown_function('watchdog', 'memcache', 'Invalid cache id received in memcache.inc wildcards() of type !type.', ['!type' => gettype($cid)], WATCHDOG_ERROR);
      return $matching;
    }
    $length = strlen($cid);

    if (isset($this->wildcard_flushes[$this->bin]) && is_array($this->wildcard_flushes[$this->bin])) {
      // Wildcard flushes per table are keyed by a substring equal to the
      // shortest wildcard clear on the table so far. So if the shortest
      // wildcard was "links:foo:", and the cid we're checking for is
      // "links:bar:bar", then the key will be "links:bar:".
      $keys = array_keys($this->wildcard_flushes[$this->bin]);
      $wildcard_length = strlen(reset($keys));
      $wildcard_key = substr($cid, 0, $wildcard_length);

      // Determine which lookups we need to perform to determine whether or not
      // our cid was impacted by a wildcard flush.
      $lookup = [];

      // Find statically cached wildcards, and determine possibly matching
      // wildcards for this cid based on a history of the lengths of past
      // valid wildcard flushes in this bin.
      if (isset($this->wildcard_flushes[$this->bin][$wildcard_key])) {
        foreach ($this->wildcard_flushes[$this->bin][$wildcard_key] as $flush_length => $timestamp) {
          if ($length >= $flush_length && $timestamp >= (REQUEST_TIME - $this->wildcard_lifetime)) {
            $wildcard = '.wildcard-' . substr($cid, 0, $flush_length);
            if (isset($wildcards[$this->bin][$wildcard])) {
              $matching[$wildcard] = $wildcards[$this->bin][$wildcard];
            }
            else {
              $lookup[$wildcard] = $wildcard;
            }
          }
        }
      }

      // Do a multi-get to retrieve all possibly matching wildcard flushes.
      if (!empty($lookup)) {
        $values = dmemcache_get_multi($lookup, $this->bin, $this->memcache);
        if (is_array($values)) {
          // Prepare an array of matching wildcards.
          $matching = array_merge($matching, $values);
          // Store matches in the static cache.
          if (isset($wildcards[$this->bin])) {
            $wildcards[$this->bin] = array_merge($wildcards[$this->bin], $values);
          }
          else {
            $wildcards[$this->bin] = $values;
          }
          $lookup = array_diff_key($lookup, $values);
        }

        // Also store failed lookups in our static cache, so we don't have to
        // do repeat lookups on single page loads.
        foreach ($lookup as $key => $key) {
          $wildcards[$this->bin][$key] = 0;
        }
      }
    }

    if ($flush) {
      $key_length = $length;
      if (isset($this->wildcard_flushes[$this->bin])) {
        $keys = array_keys($this->wildcard_flushes[$this->bin]);
        $key_length = strlen(reset($keys));
      }
      $key = substr($cid, 0, $key_length);
      // Avoid too many calls to variable_set() by only recording a flush for
      // a fraction of the wildcard invalidation variable, per cid length.
      // Defaults to 28 / 4, or one week.
      if (!isset($this->wildcard_flushes[$this->bin][$key][$length]) || (REQUEST_TIME - $this->wildcard_flushes[$this->bin][$key][$length] > $this->wildcard_lifetime / 4)) {

        // If there are more than 50 different wildcard keys for this bin
        // shorten the key by one, this should reduce variability by
        // an order of magnitude and ensure we don't use too much memory.
        if (isset($this->wildcard_flushes[$this->bin]) && count($this->wildcard_flushes[$this->bin]) > 50) {
          $key = substr($cid, 0, $key_length - 1);
          $length = strlen($key);
        }

        // If this is the shortest key length so far, we need to remove all
        // other wildcards lengths recorded so far for this bin and start
        // again. This is equivalent to a full cache flush for this table, but
        // it ensures the minimum possible number of wildcards are requested
        // along with cache consistency.
        if ($length < $key_length) {
          $this->wildcard_flushes[$this->bin] = [];
          $this->cacheStateSet('flush_timestamp', $this->bin, REQUEST_TIME);
          $this->flush_timestamp = REQUEST_TIME;
          $this->set_cids = [];
        }
        $key = substr($cid, 0, $key_length);
        $this->wildcard_flushes[$this->bin][$key][$length] = REQUEST_TIME;

        $this->cacheStateSet('wildcard_flushes', NULL, $this->wildcard_flushes);
      }
      $key = '.wildcard-' . $cid;
      if (isset($wildcards[$this->bin][$key])) {
        $wildcards[$this->bin][$key]++;
      }
      else {
        $wildcards[$this->bin][$key] = 1;
      }
      dmemcache_set($key, $wildcards[$this->bin][$key], 0, $this->bin);
    }
    return $matching;
  }

  /**
   * Check if a wildcard flush has invalidated the current cached copy.
   */
  protected function wildcardValid($cid, $cache) {
    // Previously cached content won't have ->flushes defined.  We could
    // force flush, but instead leave this up to the site admin.
    $flushes = isset($cache->flushes) ? (int) $cache->flushes : 0;
    if ($flushes < $this->wildcardFlushes($cid)) {
      return FALSE;
    }
    return TRUE;
  }

  /**
   * Implements BackdropCacheInterface::isEmpty().
   */
  public function isEmpty() {
    // We do not know so err on the safe side?
    return FALSE;
  }

  /**
   * Helper function to load locking framework if not already loaded.
   *
   * @return bool
   *   Whether the locking system was initialized successfully. This must always
   *   return TRUE or throw an exception.
   */
  public function lockInit() {
    // On a cache miss when page_cache_without_database is enabled, we can end
    // up here without the lock system being initialized. Bootstrap backdrop far
    // enough to load the lock system.
    if (!function_exists('lock_acquire')) {
      try {
        backdrop_bootstrap(BACKDROP_BOOTSTRAP_LOCK, FALSE);
      } catch (Exception $e) {}
    }

    if (!function_exists('lock_acquire')) {
      // Bootstrap failed, log error.
      register_shutdown_function('watchdog', 'memcache', 'Bootstrap failed in lockInit(), lock_acquire() is not available. (phase:!phase)', ['!phase' => backdrop_get_bootstrap_phase()], WATCHDOG_ERROR);
      return FALSE;
    }

    return TRUE;
  }

  /**
   * Determines whether stampede protection is enabled for a given bin/cid.
   *
   * Memcache stampede protection is primarily designed to benefit the following
   * caching pattern: a miss on a cache_get for a specific cid is immediately
   * followed by a cache_set for that cid. In cases where this pattern is not
   * followed, stampede protection can be disabled to avoid long hanging locks.
   * For example, a cache miss in Backdrop core's module_implements() won't
   * execute a cache_set until backdrop_page_footer() calls
   * module_implements_write_cache() which can occur much later in page
   * generation.
   *
   * @param string $cid
   *   The cache id of the data to retrieve.
   *
   * @return bool
   *   Returns TRUE if stampede protection is enabled for that particular cache
   *   bin/cid, otherwise FALSE.
   */
  protected function stampedeProtected($cid) {
    $ignore_settings = settings_get('memcache_stampede_protection_ignore', [
      // Disable stampede protection for specific cids in 'cache_bootstrap'.
      'cache_bootstrap' => [
        // The module_implements cache is written after finishing the request.
        'module_implements',
        // Variables have their own lock protection.
        'variables',
        // Delayed set.
        'lookup_cache',
        // Both schema and the theme_registry uses BackdropCacheArray, which sets
        // the cache entry with a class destructor.
        'schema:runtime:*',
        'theme_registry:runtime:*',
      ],
      // Disable stampede protection for cid prefix in 'cache'.
      'cache' => [
        // I18n uses a class destructor to set the cache.
        'i18n:string:*',
      ],
      // Delayed set.
      'cache_path',
      // Disable stampede protection for the contrib cache_rules bin as recent
      // versions of the rules module provides its own stampede protection.
      'cache_rules',
    ]);

    // Support ignoring an entire bin.
    if (in_array($this->bin, $ignore_settings)) {
      return FALSE;
    }

    // Support ignoring by cids.
    if (isset($ignore_settings[$this->bin])) {
      // Support ignoring specific cids.
      if (in_array($cid, $ignore_settings[$this->bin])) {
        return FALSE;
      }
      // Support ignoring cids starting with a suffix.
      foreach ($ignore_settings[$this->bin] as $ignore) {
        $split = explode('*', $ignore);
        if (count($split) > 1 && strpos($cid, $split[0]) === 0) {
          return FALSE;
        }
      }
    }

    return TRUE;
  }

  /**
   * Helper function to reload variables.
   *
   * This is used by the tests to verify that the cache object used the correct
   * settings.
   */
  public function reloadVariables() {
    $this->wildcard_lifetime = settings_get('memcache_wildcard_lifetime', MEMCACHE_WILDCARD_INVALIDATE);
    $this->set_cids = [];

    $this->wildcard_flushes = $this->cacheStateGet('wildcard_flushes', NULL, []);
    $this->flush_timestamp = $this->cacheStateGet('flush_timestamp', $this->bin, 0);
    $this->garbage_collection_timestamp = $this->cacheStateGet('garbage_collection_timestamp', $this->bin, 0);
    $this->flushed = min($this->flush_timestamp, REQUEST_TIME);
  }

  static function cacheStateGet($state_setting_name, $bin = NULL, $default = NULL) {
    // Prevent conflicts and indicate this is a special cache entry.
    $state_setting_name = '_' . $state_setting_name;
    $value = dmemcache_get($state_setting_name, 'bootstrap');
    // If this is a bin-specific setting, check for the key within that array.
    if ($bin) {
      $value = $value[$bin] ?? $default;
    }
    return $value ?? $default;
  }

  static function cacheStateSet($state_setting_name, $bin, $value) {
    // Prevent conflicts and indicate this is a special cache entry.
    $state_setting_name = '_' . $state_setting_name;

    // Retrieve current values if setting a bin-specific state value.
    if ($bin) {
      $array = dmemcache_get($state_setting_name, 'bootstrap');
      $array = $array ?? [];
      $array[$bin] = $value;
      dmemcache_set($state_setting_name, $array, 0, 'bootstrap');
    }
    else {
      dmemcache_set($state_setting_name, $value, 0, 'bootstrap');
    }
  }

}
