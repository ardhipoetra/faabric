#pragma once

#include "faabric_utils.h"
#include "fixtures.h"

#include "DistTestExecutor.h"

#include <faabric/scheduler/ExecutorFactory.h>
#include <faabric/scheduler/Scheduler.h>
#include <faabric/snapshot/SnapshotRegistry.h>

#define INTER_MPI_TEST_SLEEP 1500

namespace tests {
class DistTestsFixture
  : public SchedulerFixture
  , public ConfFixture
  , public SnapshotRegistryFixture
  , public PointToPointBrokerFixture
{
  public:
    DistTestsFixture()
    {
        // Make sure the host list is up to date
        sch.addHostToGlobalSet(getMasterIP());
        sch.addHostToGlobalSet(getWorkerIP());
        sch.removeHostFromGlobalSet(LOCALHOST);

        // Set up executor
        std::shared_ptr<tests::DistTestExecutorFactory> fac =
          std::make_shared<tests::DistTestExecutorFactory>();
        faabric::scheduler::setExecutorFactory(fac);
    }

    ~DistTestsFixture() = default;

    std::string getWorkerIP()
    {
        if (workerIP.empty()) {
            workerIP = faabric::util::getIPFromHostname("dist-test-server");
        }

        return workerIP;
    }

    std::string getMasterIP() { return conf.endpointHost; }

  protected:
    std::string workerIP;
    std::string mainIP;
};

class MpiDistTestsFixture : public DistTestsFixture
{
  public:
    MpiDistTestsFixture() {}

    ~MpiDistTestsFixture() = default;

  protected:
    int nLocalSlots = 2;
    int worldSize = 4;
    bool origIsMsgOrderingOn;

    void updateLocalSlots(int newLocalSlots, int newUsedLocalSlots = 0)
    {
        faabric::HostResources localRes;
        localRes.set_slots(newLocalSlots);
        localRes.set_usedslots(newUsedLocalSlots);
        sch.setThisHostResources(localRes);
    }

    void updateRemoteSlots(int newRemoteSlots, int newRemoteUsedSlots = 0)
    {
        faabric::HostResources remoteRes;
        remoteRes.set_slots(newRemoteSlots);
        remoteRes.set_usedslots(newRemoteUsedSlots);
        sch.addHostToGlobalSet(workerIP,
                               std::make_shared<HostResources>(remoteRes));
    }

    void setLocalSlots(int numLocalSlots, int worldSizeIn = 0)
    {
        if (worldSizeIn > 0) {
            worldSize = worldSizeIn;
        }
        int numRemoteSlots = worldSize - numLocalSlots;

        if (numLocalSlots == numRemoteSlots) {
            updateLocalSlots(2 * numLocalSlots, numLocalSlots);
            updateRemoteSlots(numRemoteSlots);
        } else if (numLocalSlots > numRemoteSlots) {
            updateLocalSlots(numLocalSlots);
            updateRemoteSlots(numRemoteSlots);
        } else {
            SPDLOG_ERROR(
              "Unfeasible MPI world slots config (local: {} - remote: {})",
              numLocalSlots,
              numRemoteSlots);
            throw std::runtime_error("Unfeasible slots configuration");
        }
    }

    std::shared_ptr<faabric::BatchExecuteRequest> setRequest(
      const std::string& function)
    {
        auto req = faabric::util::batchExecFactory("mpi", function, 1);
        faabric::Message& msg = req->mutable_messages()->at(0);
        msg.set_ismpi(true);
        msg.set_mpiworldsize(worldSize);
        msg.set_recordexecgraph(true);

        return req;
    }

    void checkSchedulingFromExecGraph(const faabric::util::ExecGraph& execGraph)
    {
        // Build the expectation
        std::vector<std::string> expecedHosts;
        // The distributed server is hardcoded to have four slots
        int remoteSlots = 4;
        std::string remoteIp = getWorkerIP();
        std::string localIp = getMasterIP();
        // First, allocate locally as much as we can
        for (int i = 0; i < nLocalSlots; i++) {
            expecedHosts.push_back(localIp);
        }
        // Second, allocate remotely as much as we can
        int allocateRemotely =
          std::min<int>(worldSize - nLocalSlots, remoteSlots);
        for (int i = 0; i < allocateRemotely; i++) {
            expecedHosts.push_back(remoteIp);
        }
        // Lastly, overload the main with all the ranks we haven't been able
        // to allocate anywhere
        int overloadMaster = worldSize - nLocalSlots - allocateRemotely;
        for (int i = 0; i < overloadMaster; i++) {
            expecedHosts.push_back(localIp);
        }

        // Check against the actual scheduling decision
        REQUIRE(expecedHosts ==
                faabric::util::getMpiRankHostsFromExecGraph(execGraph));
    }

    // Specialisation for migration tests
    void checkSchedulingFromExecGraph(
      const faabric::util::ExecGraph& execGraph,
      const std::vector<std::string> expectedHostsBefore,
      const std::vector<std::string> expectedHostsAfter)
    {
        auto actualHostsBeforeAndAfter =
          faabric::util::getMigratedMpiRankHostsFromExecGraph(execGraph);

        REQUIRE(actualHostsBeforeAndAfter.first == expectedHostsBefore);
        REQUIRE(actualHostsBeforeAndAfter.second == expectedHostsAfter);
    }

    void checkAllocationAndResult(
      std::shared_ptr<faabric::BatchExecuteRequest> req,
      int timeoutMs = 10000,
      bool skipExecGraphCheck = false)
    {
        faabric::Message& msg = req->mutable_messages()->at(0);
        faabric::Message result = plannerCli.getMessageResult(msg, timeoutMs);
        REQUIRE(result.returnvalue() == 0);
        SLEEP_MS(1000);
        if (!skipExecGraphCheck) {
            auto execGraph = faabric::util::getFunctionExecGraph(msg);
            checkSchedulingFromExecGraph(execGraph);
        }
    }

    void checkAllocationAndResultMigration(
      std::shared_ptr<faabric::BatchExecuteRequest> req,
      const std::vector<std::string>& expectedHostsBefore,
      const std::vector<std::string>& expectedHostsAfter,
      int timeoutMs = 1000)
    {
        faabric::Message& msg = req->mutable_messages()->at(0);
        faabric::Message result = plannerCli.getMessageResult(msg, timeoutMs);

        if (result.returnvalue() != MIGRATED_FUNCTION_RETURN_VALUE) {
            REQUIRE(result.returnvalue() == 0);
        }
        SLEEP_MS(1000);
        auto execGraph = faabric::util::getFunctionExecGraph(msg);
        checkSchedulingFromExecGraph(
          execGraph, expectedHostsBefore, expectedHostsAfter);
    }
};
}
