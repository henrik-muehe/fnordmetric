/**
 * This file is part of the "FnordMetric" project
 *   Copyright (c) 2011-2014 Paul Asmuth, Google Inc.
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <stdlib.h>
#include <stdio.h>
#include <fnord-base/application.h>
#include <fnord-base/cli/flagparser.h>
#include <fnord-base/exception.h>
#include <fnord-base/exceptionhandler.h>
#include <fnord-base/inspect.h>
#include <fnord-base/random.h>
#include <fnord-base/io/fileutil.h>
#include <fnord-base/io/inputstream.h>
#include <fnord-base/io/outputstream.h>
#include <fnord-base/logging.h>
#include <fnord-base/net/udpserver.h>
#include <fnord-base/stats/statsd.h>
#include <fnord-base/stdtypes.h>
#include <fnord-base/thread/threadpool.h>
#include <fnord-base/thread/eventloop.h>
#include <fnord-http/httpserver.h>
#include <fnord-http/httprouter.h>
#include <fnord-json/json.h>
#include <fnord-json/jsonrpc.h>
#include <fnord-metricdb/metricservice.h>
#include <fnord-metricdb/httpapiservlet.h>
#include <environment.h>

using fnord::metric_service::MetricService;
using namespace fnordmetric;

static const char kCrashErrorMsg[] =
    "FnordMetric crashed :( -- Please report a bug at "
    "github.com/paulasmuth/fnordmetric";

static MetricService makeMetricService(
    const std::string& backend_type,
    TaskScheduler* backend_scheduler) {

  /* open inmemory backend */
  if (backend_type == "inmemory") {
    fnord::logInfo("fnordmetric-server", "Opening new inmemory backend");
    return MetricService::newWithInMemoryBackend();
  }

  /* open disk backend */
  if (backend_type == "disk") {
    if (!env()->flags()->isSet("datadir")) {
      RAISE(
          kUsageError,
          "the --datadir flag must be set when using the disk backend");
    }

    auto datadir = env()->flags()->getString("datadir");

    if (!fnord::FileUtil::exists(datadir)) {
      RAISEF(kIOError, "File $0 does not exist", datadir);
    }

    if (!fnord::FileUtil::isDirectory(datadir)) {
      RAISEF(kIOError, "File $0 is not a directory", datadir);
    }

    fnord::logInfo("fnordmetric-server", "Opening disk backend at $0", datadir);
    return MetricService::newWithDiskBackend(datadir, backend_scheduler);
  }

  RAISEF(kUsageError, "unknown backend type: $0", backend_type);
}

static int startServer() {
  /* Setup statsd server */
  return 0;
}

static void printUsage() {
  auto err_stream = fnord::OutputStream::getStderr();
  err_stream->printf("usage: fnordmetric-server [options]\n");
  err_stream->printf("\noptions:\n");
  env()->flags()->printUsage(err_stream.get());
  err_stream->printf("\nexamples:\n");
  err_stream->printf("    $ fnordmetric-server --http_port 8080 --statsd_port 8125 --datadir /tmp/fnordmetric-data\n");
}

int main(int argc, const char** argv) {
  fnord::Application::init();
  fnord::Application::logToStderr();

  env()->flags()->defineFlag(
      "http_port",
      cli::FlagParser::T_INTEGER,
      false,
      NULL,
      "8080",
      "Start the web interface on this port",
      "<port>");

  env()->flags()->defineFlag(
      "statsd_port",
      cli::FlagParser::T_INTEGER,
      false,
      NULL,
      "8125",
      "Start the statsd interface on this port",
      "<port>");

  env()->flags()->defineFlag(
      "storage_backend",
      cli::FlagParser::T_STRING,
      false,
      NULL,
      "disk",
      "One of 'disk', 'inmemory', 'mysql' or 'hbase'. Default: 'disk'",
      "<name>");

  env()->flags()->defineFlag(
      "datadir",
      cli::FlagParser::T_STRING,
      false,
      NULL,
      NULL,
      "Store the database in this directory (disk backend only)",
      "<path>");

  env()->flags()->defineFlag(
      "disable_external_sources",
      cli::FlagParser::T_SWITCH,
      false,
      NULL,
      NULL,
      "Disable queries against external data sources like CSV files or MySQL");

  env()->flags()->defineFlag(
      "verbose",
      cli::FlagParser::T_SWITCH,
      false,
      NULL,
      "Be verbose");

  env()->flags()->defineFlag(
      "help",
      cli::FlagParser::T_SWITCH,
      false,
      "h",
      NULL,
      "You are reading it...");

  env()->flags()->parseArgv(argc, argv);
  env()->setVerbose(env()->flags()->isSet("verbose"));

  if (env()->flags()->isSet("help")) {
    printUsage();
    return 0;
  }

  fnord::thread::EventLoop evloop;
  fnord::thread::ThreadPool server_pool;
  fnord::thread::ThreadPool worker_pool;

  fnord::json::JSONRPC rpc;
  fnord::json::JSONRPCHTTPAdapter rpc_http(&rpc);

  try {
    /* setup MetricService */
    auto metric_service = makeMetricService(
        env()->flags()->getString("storage_backend"),
        &server_pool);

    /* start http server */
    auto http_port = env()->flags()->getInt("http_port");
    fnord::logInfo(
        "fnordmeric-server",
        "Starting HTTP server on port $0",
        http_port);

    fnord::http::HTTPRouter http_router;
    fnord::http::HTTPServer http_server(&http_router, &evloop);
    http_server.listen(http_port);

    fnord::metric_service::HTTPAPIServlet metrics_api(&metric_service);
    http_router.addRouteByPrefixMatch("/metrics", &metrics_api);
    http_router.addRouteByPrefixMatch("/rpc", &rpc_http);
    //auto http_api = new HTTPAPI(metric_service.metricRepository());
    //http_server->addHandler(AdminUI::getHandler());
    //http_server->addHandler(std::unique_ptr<http::HTTPHandler>(http_api));

    /* set up statsd server */
    fnord::statsd::StatsdServer statsd_server(&evloop, &evloop);
    statsd_server.onSample([&metric_service] (
        const std::string& key,
        double value,
        const std::vector<std::pair<std::string, std::string>>& labels) {
      if (env()->verbose()) {
        fnord::logDebug(
            "statsd sample: $0=$1 $2",
            key,
            value,
            fnord::inspect(labels).c_str());
      }

      metric_service.insertSample(key, value, labels);
    });

    /* start statsd server */
    if (env()->flags()->isSet("statsd_port")) {
      auto statsd_port = env()->flags()->getInt("statsd_port");

      fnord::logInfo(
          "fnordmetric-server",
          "Starting StatsD server on port $0",
          statsd_port);

      statsd_server.listen(statsd_port);
    }

    /* start event loop */
    evloop.run();
  } catch (const fnord::Exception& e) {
    fnord::logError("fnordmetric-server", e, "FATAL ERROR");

    if (e.getTypeName() == kUsageError) {
      printUsage();
    }

    return 1;
  }

  return 0;
}
