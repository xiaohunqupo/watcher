#pragma once

#include <watcher/platform.hpp>
#if defined(PLATFORM_UNKNOWN) || defined(WATER_WATCHER_USE_WARTHOG)

/*
  @brief watcher/adapter/warthog

  A reasonably dumb adapter that works on any platform.

  This adapter beats `kqueue`, but it doesn't bean recieving
  filesystem events directly from the OS.

  This is the fallback adapter on platforms that either
    - Only support `kqueue`
    - Only support the C++ standard library
*/

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <watcher/event.hpp>

namespace water {
namespace watcher {
namespace detail {
namespace adapter {
namespace { /* anonymous namespace for "private" things */
/* clang-format off */

inline constexpr std::filesystem::directory_options
  dir_opt = 
    /* This is ridiculous */
    std::filesystem::directory_options::skip_permission_denied 
    & std::filesystem::directory_options::follow_directory_symlink;

using bucket_type = std::unordered_map<std::string, std::filesystem::file_time_type>;

/* clang-format on */

/*  @brief watcher/adapter/warthog/scan
    - Scans `path` for changes.
    - Updates our bucket to match the changes.
    - Calls `callback` when changes happen.
    - Returns false if the file tree cannot be scanned. */
bool scan(const char* path, const auto& callback, bucket_type& bucket) {
  using std::filesystem::exists, std::filesystem::is_symlink,
      std::filesystem::is_directory, std::filesystem::is_regular_file,
      std::filesystem::last_write_time, std::filesystem::is_regular_file,
      std::filesystem::recursive_directory_iterator;
  /* @brief watcher/adapter/warthog/scan_file
     - Scans a (single) file for changes.
     - Updates our bucket to match the changes.
     - Calls `callback` when changes happen.
     - Returns false if the file cannot be scanned. */
  const auto scan_file = [&](const char* file, const auto& callback) -> bool {
    if (exists(file) && is_regular_file(file)) {
      auto ec = std::error_code{};
      /* grabbing the file's last write time */
      const auto timestamp = last_write_time(file, ec);
      if (ec) {
        /* the file changed while we were looking at it. so, we call the
         * closure, indicating destruction, and remove it from the bucket. */
        callback(event::event{file, event::what::destroy, event::kind::file});
        if (bucket.contains(file))
          bucket.erase(file);
      }
      /* if it's not in our bucket, */
      else if (!bucket.contains(file)) {
        /* we put it in there and call the closure, indicating creation. */
        bucket[file] = timestamp;
        callback(event::event{file, event::what::create, event::kind::file});
      }
      /* otherwise, it is already in our bucket. */
      else {
        /* we update the file's last write time, */
        if (bucket[file] != timestamp) {
          bucket[file] = timestamp;
          /* and call the closure on them, indicating modification */
          callback(event::event{file, event::what::modify, event::kind::file});
        }
      }
      return true;
    } /* if the path doesn't exist, we nudge the callee with `false` */
    else
      return false;
  };

  /* @brief watcher/adapter/warthog/scan_directory
     - Scans a (single) directory for changes.
     - Updates our bucket to match the changes.
     - Calls `callback` when changes happen.
     - Returns false if the directory cannot be scanned. */
  const auto scan_directory = [&](const char* dir,
                                  const auto& callback) -> bool {
    /* if this thing is a directory */
    if (is_directory(dir)) {
      /* try to iterate through its contents */
      auto dir_it_ec = std::error_code{};
      for (const auto& file :
           recursive_directory_iterator(dir, dir_opt, dir_it_ec))
        /* while handling errors */
        if (dir_it_ec)
          return false;
        else
          scan_file(file.path().c_str(), callback);
      return true;
    } else
      return false;
  };

  return scan_directory(path, callback) ? true
         : scan_file(path, callback)    ? true
                                        : false;
};

/* @brief water/watcher/warthog/tend_bucket
   If the bucket is empty, try to populate it.
   otherwise, prune it. */
bool tend_bucket(const char* path, const auto& callback, bucket_type& bucket) {
  using std::filesystem::exists, std::filesystem::is_symlink,
      std::filesystem::is_directory, std::filesystem::is_regular_file,
      std::filesystem::last_write_time, std::filesystem::is_regular_file,
      std::filesystem::recursive_directory_iterator;
  /*  @brief watcher/adapter/warthog/populate
      @param path - path to monitor for
      Creates a file map, the "bucket", from `path`. */
  const auto populate = [&](const char* path) -> bool {
    /* this happens when a path was changed while we were reading it.
     there is nothing to do here; we prune later. */
    auto dir_it_ec = std::error_code{};
    auto lwt_ec = std::error_code{};
    if (exists(path)) {
      /* this is a directory */
      if (is_directory(path)) {
        for (const auto& file :
             recursive_directory_iterator(path, dir_opt, dir_it_ec)) {
          if (!dir_it_ec) {
            const auto lwt = last_write_time(file, lwt_ec);
            if (!lwt_ec)
              bucket[file.path().string()] = lwt;
            else
              /* @todo use this practice elsewhere or make a fn for it
                 otherwise, this might be confusing and inconsistent. */
              bucket[file.path().string()] = last_write_time(path);
          }
        }
      }
      /* this is a file */
      else {
        bucket[path] = last_write_time(path);
      }
    } else {
      return false;
    }
    return true;
  };

  /*  @brief watcher/adapter/warthog/prune
      Removes files which no longer exist from our bucket. */
  const auto prune = [&](const char* path, const auto& callback) -> bool {
    auto bucket_it = bucket.begin();
    /* while looking through the bucket's contents, */
    while (bucket_it != bucket.end()) {
      /* check if the stuff in our bucket exists anymore. */
      exists(bucket_it->first)
          /* if so, move on. */
          ? std::advance(bucket_it, 1)
          /* if not, call the closure, indicating destruction,
             and remove it from our bucket. */
          : [&]() {
              callback(event::event{bucket_it->first.c_str(),
                                    event::what::destroy,
                                    is_regular_file(path) ? event::kind::file
                                    : is_directory(path)  ? event::kind::dir
                                    : is_symlink(path) ? event::kind::sym_link
                                                       : event::kind::other});
              /* bucket, erase it! */
              bucket_it = bucket.erase(bucket_it);
            }();
    }
    return true;
  };

  return bucket.empty() ? populate(path)          ? true
                          : prune(path, callback) ? true
                                                  : false
                        : true;
};

} /* namespace */

/*
  @brief watcher/adapter/warthog/watch

  @param closure (optional):
   A callback to perform when the files
   being watched change.
   @see Callback

  Monitors `path` for changes.

  Calls `callback` with an `event` when they happen.

  Unless it should stop, or errors present, `watch` recurses.
*/
template <const auto delay_ms = 16>
inline bool watch(const char* path, const auto& callback) {
  using std::this_thread::sleep_for, std::chrono::milliseconds;

  static bucket_type bucket;

  if constexpr (delay_ms > 0)
    sleep_for(milliseconds(delay_ms));

  /* If:
      - The bucket is doing well, and
      - No errors occured while scanning,
    then keep running.
    Otherwise, stop and return false. */
  return tend_bucket(path, callback, bucket)
             ? scan(path, callback, bucket)
                   ? adapter::watch<delay_ms>(path, callback)
                   : false
             : false;
}

} /* namespace adapter */
} /* namespace detail */
} /* namespace watcher */
} /* namespace water */

#endif /* if defined(PLATFORM_UNKNOWN) */
