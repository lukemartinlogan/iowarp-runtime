#include "chimaera/chimaera_types.h"
#include "chimaera_admin/chimaera_admin.h"

int main() {
  CHIMAERA_CLIENT_INIT();
  std::vector<chi::WorkerStats> stats = CHI_ADMIN->PollStats(
      HSHM_DEFAULT_MEM_CTX,
      chi::DomainQuery::GetDirectHash(chi::SubDomainId::kLocalContainers, 0));
  // for (const auto &stat : stats) {
  //   std::cout << stat << std::endl;
  // }
}