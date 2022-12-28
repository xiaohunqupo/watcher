/*
   Test Watcher
   Directory Events
*/

/* REQUIRE,
   TEST_CASE */
#include <snatch/snatch.hpp>
/* milliseconds */
#include <chrono>
/* create_directory,
   create_regular_files,
   remove_regular_files,
   test_store_path,
   regular_file_store_path,
   dir_store_path */
#include <test_watcher/test_watcher.hpp>
/* thread,
   sleep_for */
#include <thread>
/* watch,
   event,
   die */
#include <watcher/watcher.hpp>

/* Test that directories are scanned */
TEST_CASE("Directory Events", "[directory_events]")
{
  /* Setup */
  using namespace wtr::watcher;
  using namespace wtr::test_watcher;
  using namespace std::chrono_literals;
  using namespace std::filesystem;
  using namespace std::this_thread;
  using std::thread, std::chrono::milliseconds;

  auto const watch_path = dir_store_path;
  auto const event_callback
      = [](auto this_event) { std::cout << this_event << std::endl; };

  show_conf("directory_events", test_store_path, watch_path);

  create_directory(test_store_path);
  create_directory(watch_path);

  sleep_for(prior_fs_events_clear_milliseconds);

  /* Start */
  show_event_stream_preamble();

  auto ms_begin = ms_now().count();

  /* Watch */
  thread([&]() { watch(watch_path.c_str(), event_callback); }).detach();

  /* Create Events */
  create_directories(watch_path, path_count);

  /* Wait */
  sleep_for(death_after_test_milliseconds);

  /* Stop Watch */
  bool const is_watch_dead = die(watch_path.c_str(), event_callback);

  auto const alive_for_ms_target_value = death_after_test_milliseconds.count();
  auto const alive_for_ms_actual_value = ms_duration(ms_begin);
  std::cout << "alive_for_ms_target_value: " << alive_for_ms_target_value
            << std::endl;
  std::cout << "alive_for_ms_actual_value: " << alive_for_ms_actual_value
            << std::endl;

  show_event_stream_postamble(alive_for_ms_target_value, is_watch_dead);

  /* Clean */
  remove_all(test_store_path);
};