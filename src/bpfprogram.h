#include "bpftrace.h"
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

namespace bpftrace {

class BpfProgram
{
public:
  static std::optional<BpfProgram> CreateFromBytecode(BpfBytecode &bytecode,
                                                      const char *name,
                                                      BPFtrace &bpftrace);

  std::tuple<uint8_t *, uintptr_t> getCode();
  BpfProgram(BpfProgram const &) = default;
  BpfProgram(BpfProgram &&) = default;

private:
  explicit BpfProgram(BpfBytecode &bytecode,
                      const char *name,
                      BPFtrace &bpftrace);

  void assemble();

  BpfBytecode &bytecode_;
  BPFtrace &bpftrace_;
  std::string name_;
  std::vector<uint8_t> code_;
};

} // namespace bpftrace
