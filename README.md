Memcache Cache Backend
======================

This project provides a cache backend to connect Backdrop websites to memcache.
Memcache can be faster than MySQL-based caches when memcache is run locally on
the same machine as PHP is running.

Requirements
------------

- Availability of a memcached daemon: http://memcached.org/
- One of the two PECL memcache packages:
  - http://pecl.php.net/package/memcache (recommended)
  - http://pecl.php.net/package/memcached (latest versions require PHP 5.2 or
    greater)

Installation
------------

These are the steps you need to take in order to use this software. Order
is important.

1. Install the memcached binaries on your server and start the memcached
   service. Follow best practices for securing the service; for example,
   lock it down so only your web servers can make connections. Find community
   maintained documentation with a number of walk-throughs for various
   operating systems at https://www.drupal.org/node/1131458.
2. Install your chosen PECL memcache extension -- this is the memcache client
   library which will be used by the Backdrop memcache module to interact with
   the memcached server(s). Generally PECL memcache (3.0.6+) is recommended,
   but PECL memcached (2.0.1+) also works well for some people. There are
   known issues with older versions. Refer to the community maintained
   documentation referenced above for more information.
3. Put your site into offline mode.
4. Download and install the memcache module.
5. If you have previously been running the memcache module, run update.php.
6. Optionally edit settings.php to configure the servers, clusters and bins
   for memcache to use. If you skip this step the Backdrop module will attempt
   to talk to the memcache server on port 11211 on the local host, storing all
   data in a single bin. This is sufficient for most smaller, single-server
   installations.
7. Edit settings.php to make memcache the default cache class, for example:
   ```
   $settings['cache_backends'][] = 'modules/memcache/memcache.inc';
   $settings['cache_default_class'] = 'BackdropMemcache';
   ```
   The cache_backends path needs to be adjusted based on where you installed
   the module.
8. Bring your site back online.

bee and drush CLIs
------------------

This module provides both bee and drush CLI support. Either tool may be used to
execute the following commands:

```
memcache-flush (mcf)  Flush all Memcached objects in a bin.
memcache-stats (mcs)  Retrieve statistics from Memcached.
```

For more information about each command, use  the `help` commands. For example:
```
bee help mcf
drush help mcf
```

Advanced Configuration
----------------------

This module is capable of working with one memcached instance or with multiple
memcached instances run across one or more servers. The default is to use one
server accessible on localhost port 11211. If that meets your needs, then the
configuration settings outlined above are sufficient for the module to work.
If you want to use multiple memcached instances, or if you are connecting to a
memcached instance located on a remote machine, further configuration is
required.

The available memcached servers are specified in $conf in settings.php. If you
do not specify any servers, memcache.inc assumes that you have a memcached
instance running on localhost:11211. If this is true, and it is the only
memcached instance you wish to use, no further configuration is required.

If you have more than one memcached instance running, you need to add two arrays
to $settings; memcache_servers and memcache_bins. The arrays follow this pattern:

```php
$settings['memcache_servers'] => array(
  'server1:port' => 'cluster1',
  'server2:port' => 'cluster2',
  'serverN:port' => 'clusterN',
  'unix:///path/to/socket' => 'clusterS',
);

$settings['memcache_bins'] => array(
   'bin1' => 'cluster1',
   'bin2' => 'cluster2',
   'binN' => 'clusterN',
   'binS' => 'clusterS',
);
```

You can optionally assign a weight to each server, favoring one server more than
another. For example, to make it 10 times more likely to store an item on
server1 versus server2:

```php
$settings['memcache_servers'] => array(
  'server1:port' => array('cluster' => 'cluster1', 'weight' => 10),
  'server2:port' => array('cluster' => 'cluster2', 'weight' => 1'),
);
```

The bin/cluster/server model can be described as follows:

- Servers are memcached instances identified by host:port.

- Clusters are groups of servers that act as a memory pool. Each cluster can
  contain one or more servers.

- Bins are groups of data that get cached together and map 1:1 to the $table
  parameter of cache_set(). Examples from Backdrop core are cache_filter and
  cache_menu. The default is 'cache'.

- Multiple bins can be assigned to a cluster.

- The default cluster is 'default'.

Locking
-------

The memcache-lock.inc file included with this module can be used as a drop-in
replacement for the database-mediated locking mechanism provided by Backdrop
core. To enable, define the following in your settings.php:

```php
$settings['lock_inc'] = 'modules/memcache/memcache-lock.inc';
```

Locks are written in the 'semaphore' table, which will map to the 'default'
memcache cluster unless you explicitly configure a 'semaphore' cluster.

Stampede Protection
-------------------

Memcache includes stampede protection for rebuilding expired cache items. To
enable stampede protection, define the following in settings.php:

```
$conf['memcache_stampede_protection'] = TRUE;
```

To avoid lock stampedes, it is important that you enable the memcache lock
implementation when enabling stampede protection -- enabling stampede protection
without enabling the Memcache lock implementation can cause worse performance
and can result in dropped locks due to key-length truncation.

Memcache stampede protection is primarily designed to benefit the following
caching pattern: a miss on a cache_get() for a specific cid is immediately
followed by a cache_set() for that cid. Of course, this is not the only caching
pattern used in Backdrop, so stampede protection can be selectively disabled for
optimal performance. For example, a cache miss in Backdrop core's
module_implements() won't execute a cache_set until backdrop_page_footer()
calls module_implements_write_cache() which can occur much later in page
generation. To avoid long hanging locks, stampede protection should be
disabled for these delayed caching patterns.

Memcache stampede protection can be disabled for entire bins, specific cid's in
specific bins, or cid's starting with a specific prefix in specific bins. For
example:

```php
$settings['memcache_stampede_protection_ignore'] = array(
  // Ignore some cids in 'cache_bootstrap'.
  'cache_bootstrap' => array(
    'module_implements',
    'variables',
    'lookup_cache',
    'schema:runtime:*',
    'theme_registry:runtime:*',
    '_backdrop_file_scan_cache',
  ),
  // Ignore all cids in the 'cache' bin starting with 'i18n:string:'
  'cache' => array(
    'i18n:string:*',
  ),
  // Disable stampede protection for the entire 'cache_path' and 'cache_rules'
  // bins.
  'cache_path',
  'cache_rules',
);
```

Only change the following stampede protection settings if you're sure you know
what you're doing, which requires first reading the memcache.inc code.

The value passed to lock_acquire, defaults to '15':
```php
$settings['memcache_stampede_semaphore'] = 15;
```

The value passed to lock_wait, defaults to 5:
```php
$settings['memcache_stampede_wait_time'] = 5;
```

The maximum number of calls to lock_wait() due to stampede protection during a
single request, defaults to 3:
```php
$settings['memcache_stampede_wait_limit'] = 3;
```

When adjusting these variables, be aware that:
 - there is unlikely to be a good use case for setting wait_time higher
   than stampede_semaphore;
 - wait_time * wait_limit is designed to default to a number less than
   standard web server timeouts (i.e. 15 seconds vs. apache's default of
   30 seconds).

Cache Header
------------

Backdrop core indicates whether or not a page was served out of the cache by
setting the 'X-Backdrop-Cache' response header with a value of HIT or MISS. If
you'd like to confirm whether pages are actually being retrieved from Memcache
and not another backend, you can enable the following option:

```php
$settings['memcache_pagecache_header'] = TRUE;
```

When enabled, the Memcache module will add its own 'Backdrop-PageCache-Memcache'
header. When cached pages are served out of the cache the header will include an
'age=' value indicating how many seconds ago the page was stored in the cache.

Persistent Connections
----------------------

The memcache module uses persistent connections by default. If this causes you
problems you can disable persistent connections by adding the  following to your
settings.php:

```
$settings['memcache_persistent'] = FALSE;
```

This setting works independently from stampede support, though it changes the
time at which timestamp-cached items are considered expired, and therefore
affects the time at which stampede behavior happens (if enabled).

## EXAMPLES ##

Example 1:

First, the most basic configuration which consists of one memcached instance
running on localhost port 11211 and all caches except for cache_form being
stored in memcache. We also enable stampede protection, and the memcache
locking mechanism. Finally, we tell Backdrop to not bootstrap the database when
serving cached pages to anonymous visitors.

```
$settings['cache_backends'][] = 'modules/memcache/memcache.inc';
$settings['lock_inc'] = 'modules/memcache/memcache-lock.inc';
$settings['memcache_stampede_protection'] = TRUE;
$settings['cache_default_class'] = 'BackdropMemcache';
```

Note that no servers or bins are defined.  The default server and bin
configuration which is used in this case is equivalent to setting:

```
$settings['memcache_servers'] = array('localhost:11211' => 'default');
```


Example 2:

In this example we define three memcached instances, two accessed over the
network, and one on a Unix socket -- please note this is only an illustration of
what is possible, and is not a recommended configuration as it's highly unlikely
you'd want to configure memcache to use both sockets and network addresses like
this, instead you'd consistently use one or the other.

The instance on port 11211 belongs to the 'default' cluster where everything
gets cached that isn't otherwise defined. (We refer to it as a "cluster", but in
this example our "clusters" involve only one instance.) The instance on port
11212 belongs to the 'pages' cluster, with the 'cache_page' table mapped to
it -- so the Backdrop page cache is stored in this cluster.  Finally, the instance
listening on a socket is part of the 'entity_node' cluster, with the
'cache_entity_node' table mapped to it -- so the Backdrop block cache is stored
here. Note that sockets do not have ports.

```
$settings['cache_backends'][] = 'sites/all/modules/memcache/memcache.inc';
$settings['lock_inc'] = 'sites/all/modules/memcache/memcache-lock.inc';
$settings['memcache_stampede_protection'] = TRUE;
$settings['cache_default_class'] = 'BackdropMemcache';

// Important to define a default cluster in both the servers
// and in the bins. This links them together.
$settings['memcache_servers'] = array(
  '10.1.1.1:11211' => 'default',
  '10.1.1.1:11212' => 'pages',
  'unix:///path/to/socket' => 'entity_node',
);
$settings['memcache_bins'] = array(
  'cache' => 'default',
  'cache_page' => 'pages',
  'cache_entity_node' => 'entity_node',
);
```


Example 3:

Here is an example configuration that has two clusters, 'default' and
'cluster2'. Five memcached instances running on four different servers are
divided up between the two clusters. The 'cache_filter' and 'cache_menu' bins
go to 'cluster2'. All other bins go to 'default'.

```php
$settings['cache_backends'][] = 'sites/all/modules/memcache/memcache.inc';
$settings['lock_inc'] = 'sites/all/modules/memcache/memcache-lock.inc';
$settings['memcache_stampede_protection'] = TRUE;
$settings['cache_default_class'] = 'BackdropMemcache';

$settings['memcache_servers'] = array(
  '10.1.1.6:11211' => 'default',
  '10.1.1.6:11212' => 'default',
  '10.1.1.7:11211' => 'default',
  '10.1.1.8:11211' => 'cluster2',
  '10.1.1.9:11211' => 'cluster2',
);

$settings['memcache_bins'] = array(
  'cache' => 'default',
  'cache_filter' => 'cluster2',
  'cache_menu' => 'cluster2',
);
```

Prefixing
---------

If you want to have multiple Backdrop installations share memcached instances,
you need to include a unique prefix for each Backdrop installation in the $conf
array of settings.php. This can be a single string prefix, or a keyed array of
bin => prefix pairs:

```php
$settings['memcache_key_prefix'] = 'something_unique';
```

Using a per-bin prefix:

```php
$settings['memcache_key_prefix'] = array(
  'default' => 'something_unique',
  'cache_page' => 'something_else_unique',
);
```

In the above example, the 'something_unique' prefix will be used for all bins
except for the 'cache_page' bin which will use the 'something_else_unique'
prefix. Note that if using a keyed array for specifying prefix, you must specify
the 'default' prefix.

It is also possible to specify multiple prefixes per bin. Only the first prefix
will be used when setting/getting cache items, but all prefixes will be cleared
when deleting cache items. This provides support for more complicated
configurations such as a live instance and an administrative instance each with
their own prefixes and therefore their own unique caches. Any time a cache item
is deleted on either instance, it gets flushed on both -- thus, should an admin
do something that flushes the page cache, it will appropriately get flushed on
both instances. (For more discussion see the issue where support was added,
https://www.drupal.org/node/1084448.) This feature is enabled when you configure
prefixes as arrays within arrays. For example:

```php
// Live instance.
$settings['memcache_key_prefix'] = array(
  'default' => array(
    'live_unique', // live cache prefix
    'admin_unique', // admin cache prefix
  ),
);
```

The above would be the configuration of your live instance. Then, on your
administrative instance you would flip the keys:

```php
// Administrative instance.
$settings['memcache_key_prefix'] = array(
  'default' => array(
    'admin_unique', // admin cache prefix
    'live_unique', // live cache prefix
  ),
);
```

Alternative Serializers
-----------------------

To optimize how data is serialized before it is written to memcache, you can
enable either the igbinary or msgpack PECL extension. Both switch from using
PHP's own human-readable serialized data structures to more compact binary
formats.

No special configuration is required.  If both extensions are enabled,
memcache will automatically use the igbinary extension. If only one extension
is enabled, memcache will automatically use that extension.

You can optionally specify which extension is used by adding one of the
following to your settings.php:

```
// Force memcache to use PHP's core serialize functions
$settings['memcache_serialize'] = 'serialize';

// Force memcache to use the igbinary serialize functions (if available)
$settings['memcache_serialize'] = 'igbinary';

// Force memcache to use the msgpack serialize functions (if available)
$settings['memcache_serialize'] = 'msgpack';
```

To review which serialize function is being used, enable the memcache_admin
module and visit admin/reports/memcache.

### igbinary

The igbinary project is maintained on GitHub:
 - https://github.com/igbinary/igbinary

The official igbinary PECL extension can be found at:
 - https://pecl.php.net/package/igbinary

### msgpack:

The msgpack project is maintained at:
  - https://msgpack.org

The official msgpack PECL extension can be found at:
  - https://pecl.php.net/package/msgpack

Maximum Lengths
---------------

If the length of your prefix + key + bin combine to be more than 250 characters,
they will be automatically hashed. Memcache only supports key lengths up to 250
bytes. You can optionally configure the hashing algorithm used, however sha1 was
selected as the default because it performs quickly with minimal collisions.

Visit http://www.php.net/manual/en/function.hash-algos.php to learn more about
which hash algorithms are available.

```
$settings['memcache_key_hash_algorithm'] = 'sha1';
```

You can also tune the maximum key length BUT BE AWARE this doesn't affect
memcached's server-side limitations -- this value is primarily exposed to allow
you to further shrink the length of keys to optimize network performance.
Specifying a length larger than 250 will almost certainly lead to problems
unless you know what you're doing.

```
$settings['memcache_key_max_length'] = 250;
```

By default, the memcached server can store objects up to 1 MiB in size. It's
possible to increase the memcached page size to support larger objects, but this
can also lead to wasted memory. Alternatively, the memcache module splits
these large objects into smaller pieces. By default, the memcache module
splits objects into 1 MiB sized pieces. You can modify this with the following
tunable to match any special server configuration you may have. NOTE: Increasing
this value without making changes to your memcached server can result in
failures to cache large items.

(Note: 1 MiB = 1024 x 1024 = 1048576.)

```
$settings['memcache_data_max_length'] = 1048576;
```

It is generally undesirable to store excessively large objects in memcache as
this can result in a performance penalty. Because of this, by default the
memcache module logs any time an object is cached that has to be split into
multiple pieces. If this is generating too many watchdog logs, you should first
understand why these objects are so large and if anything can be done to make
them smaller. If you determine that the large size is valid and is not causing
you any unnecessary performance penalty, you can tune the following variable to
minimize or disable this logging. Set the value to a positive integer to only
log when an object is split into this many or more pieces. For example, if
memcache_data_max_length is set to 1048576 and memcache_log_data_pieces is set
to 5, watchdog logs will only be written when an object is split into 5 or more
pieces (objects >4 MiB in size). Or, to completely disable logging set
memcache_log_data_pieces to 0 or FALSE.

```
$settings['memcache_log_data_pieces'] = 2;
```

Multiple Servers
----------------

To use this module with multiple memcached servers, it is important that you set
the hash strategy to consistent. This is controlled in the PHP extension, not
the Backdrop module.

If using PECL memcache:
Edit /etc/php.d/memcache.ini (path may changed based on package/distribution)
and set the following:
```
memcache.hash_strategy=consistent
```

You need to reload apache httpd after making that change.

If using PECL memcached:
Memcached options can be controlled in settings.php. The following setting is
needed:

```
$settings['memcache_options'] = array(
  Memcached::OPT_DISTRIBUTION => Memcached::DISTRIBUTION_CONSISTENT,
);
```

Debug Logging
-------------

You can optionally enable debug logging by adding the following to your
settings.php:

```
$settings['memcache_debug_log'] = '/path/to/file.log';
```

By default, only the following memcache actions are logged: 'set', 'add',
'delete', and 'flush'. If you'd like to also log 'get' and 'getMulti' actions,
enble verbose logging:

```
$settings['memcache_debug_verbose'] = TRUE;
```

This file needs to be writable by the web server (and/or by drush) or you will
see lots of watchdog errors. You are responsible for ensuring that the debug log
doesn't get too large. By default, enabling debug logging will write logs
looking something like:

```
1484719570|add|semaphore|semaphore-memcache_system_list%3Acache_bootstrap|1
1484719570|set|cache_bootstrap|cache_bootstrap-system_list|1
1484719570|delete|semaphore|semaphore-memcache_system_list%3Acache_bootstrap|1
```

The default log format is pipe delineated, containing the following fields:

```
timestamp|action|bin|cid|return code
```

You can specify a custom log format by setting the memcache_debug_log_format
variable. Supported variables that will be replaced in your format are:
'!timestamp', '!action', '!bin', '!cid', and '!rc'.
For example, the default log format (note that it includes a new line at the
end) is:

```
$settings['memcache_debug_log_format'] = "!timestamp|!action|!bin|!cid|!rc\n";
```

You can change the timestamp format by specifying a PHP date() format string in
the memcache_debug_time_format variable. PHP date() formats are documented at
http://php.net/manual/en/function.date.php. By default timestamps are written as
a unix timestamp. For example:

```
$settings['memcache_debug_time_format'] = 'U';
```

Toubleshooting
--------------

PROBLEM:
 Error:
  Failed to load required file memcache/dmemcache.inc
 Or:
 cache_backends not properly configured in settings.php, failed to load
 required file memcache.inc

SOLUTION:
You need to enable memcache in settings.php. Search for "Example 1" above
for a basic configuration example.

PROBLEM:
 Error:
  PECL !extension version %version is unsupported. Please update to
  %recommended or newer.

SOLUTION:
Upgrade to the latest available PECL extension release. Older PECL extensions
have known bugs and cause a variety of problems when using the memcache module.

PROBLEM:
 Error:
  Failed to connect to memcached server instance at <IP ADDRESS>.

SOLUTION:
Verify that the memcached daemon is running at the specified IP and PORT. To
debug you can try to telnet directly to the memcache server from your web
servers, example:
   telnet localhost 11211

PROBLEM:
 Error:
  Failed to store to then retrieve data from memcache.

SOLUTION:
Carefully review your settings.php configuration against the above
documentation. This error simply does a cache_set followed by a cache_get
and confirms that what is written to the cache can then be read back again.
This test was added in the 7.x-1.1 release.

The following code is what performs this test -- you can wrap this in a <?php
tag and execute as a script with 'drush scr' to perform further debugging.

```
$cid = 'memcache_requirements_test';
$value = 'OK';
// Temporarily store a test value in memcache.
cache_set($cid, $value);
// Retreive the test value from memcache.
$data = cache_get($cid);
if (!isset($data->data) || $data->data !== $value) {
  echo t('Failed to store to then retrieve data from memcache.');
}
else {
  // Test a delete as well.
  cache_clear_all($cid, 'cache');
}
```

PROBLEM:
 Error:
  Unexpected failure when testing memcache configuration.

SOLUTION:
Be sure the memcache module is properly installed, and that your settings.php
configuration is correct. This error means an exception was thrown when
attempting to write to and then read from memcache.

PROBLEM:
 Error:
  Failed to set key: Failed to set key: cache_page-......

SOLUTION:
Upgrade your PECL library to PECL package (2.2.1) (or higher).

WARNING:
Zlib compression at the php.ini level and Memcache conflict.
See http://drupal.org/node/273824

Memcache Admin
--------------

A module offering a UI for memcache is included. It provides aggregated and
per-page statistics for memcache.

Memcached PECL Extension Support
--------------------------------

We also support the Memcached PECL extension. This extension backends
to libmemcached and allows you to use some of the newer advanced features in
memcached 1.4.

NOTE: It is important to realize that the memcache php.ini options do not impact
the memcached extension, this new extension doesn't read in options that way.
Instead, it takes options directly from Backdrop. Because of this, you must
configure memcached in settings.php. Please look here for possible options:

http://us2.php.net/manual/en/memcached.constants.php

An example configuration block is below, this block also illustrates our
default options (selected through performance testing). These options will be
set unless overridden in settings.php.

```
$settings['memcache_options'] = array(
  Memcached::OPT_DISTRIBUTION => Memcached::DISTRIBUTION_CONSISTENT,
);
```

Other options you could experiment with:

- `Memcached::OPT_COMPRESSION => FALSE`
  - This disables compression in the Memcached extension. This may save some
    CPU cost, but can result in significantly more data being transmitted and
    stored. See: https://www.drupal.org/project/memcache/issues/2958403

- `Memcached::OPT_BINARY_PROTOCOL => TRUE`
  - This enables the Memcache binary protocol (only available in Memcached
    1.4 and later). Note that some users have reported SLOWER performance
    with this feature enabled. It should only be enabled on extremely high
    traffic networks where memcache network traffic is a bottleneck.
    Additional reading about the binary protocol:
    http://code.google.com/p/memcached/wiki/MemcacheBinaryProtocol

- `Memcached::OPT_TCP_NODELAY => TRUE`
  - This enables the no-delay feature for connecting sockets; it's been
    reported that this can speed up the Binary protocol (see above). This
    tells the TCP stack to send packets immediately and without waiting for
    a full payload, reducing per-packet network latency (disabling "Nagling").

## Authentication

### Binary Protocol SASL Authentication

SASL authentication can be enabled as documented here:
  http://php.net/manual/en/memcached.setsaslauthdata.php
  https://code.google.com/p/memcached/wiki/SASLHowto

SASL authentication requires a memcached server with SASL support (version 1.4.3
or greater built with --enable-sasl and started with the -S flag) and the PECL
memcached client version 2.0.0 or greater also built with SASL support. Once
these requirements are satisfied you can then enable SASL support in the Backdrop
memcache module by enabling the binary protocol and setting
memcache_sasl_username and memcache_sasl_password in settings.php. For example:

```
$settings['memcache_options'] = array(
  Memcached::OPT_BINARY_PROTOCOL => TRUE,
);
$settings['memcache_sasl_username'] = 'yourSASLUsername';
$settings['memcache_sasl_password'] = 'yourSASLPassword';
```

### ASCII Protocol Authentication

If you do not want to enable the binary protocol, you can instead enable
token authentication with the default ASCII protocol.

ASCII protocol authentication requires Memcached version 1.5.15 or greater
started with the -Y flag, and the PECL memcached client. It was originally
documented in the memcached 1.5.15 release notes:
  https://github.com/memcached/memcached/wiki/ReleaseNotes1515

While it will work with 1.5.15 or greater, it's strongly recommended you
use memcached 1.6.4 or greater due to the following bug fix:
  https://github.com/memcached/memcached/wiki/ReleaseNotes164

Additional detail about this feature can be found in the protocol documentation:
  https://github.com/memcached/memcached/blob/master/doc/protocol.txt

All your memcached servers need to be started with the -Y option to specify
a local path to an authfile which can contain up to 8 "username:pasword"
pairs, any of which can be used for authentication. For example, a simple
authfile may look as follows:

```
foo:bar
```

You can then configure your website to authenticate with this username and
password as follows:

```
$conf['memcache_ascii_auth'] = 'foo bar';
```

Enabling ASCII protocol authentication during load testing resulted in less than
1% overhead.

## Amazon Elasticache

You can use the Backdrop Memcache module to talk with Amazon Elasticache, but to
enable Automatic Discovery you must use Amazon's forked version of the PECL
Memcached extension with Dynamic Client Mode enabled.

Their PECL Memcached fork is maintained on GitHub:
 - https://github.com/awslabs/aws-elasticache-cluster-client-memcached-for-php

If you are using PHP 7 you need to select the php7 branch of their project.

Once the extension is installed, you can enable Dynamic Client Mode as follows:

```
$settings['memcache_options'] = array(
  Memcached::OPT_DISTRIBUTION => Memcached::DISTRIBUTION_CONSISTENT,
  Memcached::OPT_CLIENT_MODE  => Memcached::DYNAMIC_CLIENT_MODE,
);
```

You then configure the module normally. Amazon explains:
  "If you use Automatic Discovery, you can use the cluster's Configuration
   Endpoint to configure your Memcached client."

The Configuration Endpoint must have 'cfg' in the name or it won't work. Further
documentation can be found here:
http://docs.aws.amazon.com/AmazonElastiCache/latest/UserGuide/Endpoints.html

If you don't want to use Automatic Discovery you don't need to install the
forked PECL extension, Amazon explains:
  "If you don't use Automatic Discovery, you must configure your client to use
   the individual node endpoints for reads and writes. You must also keep track
   of them as you add and remove nodes."
