#include "bpfprogram.h"
#include "log.h"

#include <cstdint>
#include <cstring>
#include <elf.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>

namespace bpftrace {

// We currently support building against really old
// kernel/elf headers. These defines provide the information
// that might be missing and are a stopgap until a high
// enough libbpf is a requirement and this code can be removed.

#ifndef R_BPF_64_64
#define R_BPF_64_64 1
#endif

#ifndef BPF_PSEUDO_FUNC
#define BPF_PSEUDO_FUNC 4
#endif

struct btf_ext_header
{
  __u16 magic;
  __u8 version;
  __u8 flags;
  __u32 hdr_len;

  /* All offsets are in bytes relative to the end of this header */
  __u32 func_info_off;
  __u32 func_info_len;
};

struct btf_ext_info_sec
{
  __u32 sec_name_off; /* offset to section name */
  __u32 num_info;
  /* Followed by num_info * record_size number of bytes */
  __u8 data[0];
};

std::optional<BpfProgram> BpfProgram::CreateFromBytecode(BpfBytecode &bytecode,
                                                         const char *name,
                                                         BPFtrace &bpftrace)
{
  if (bytecode.find(name) != bytecode.end())
  {
    return BpfProgram(bytecode, name, bpftrace);
  }
  return std::nullopt;
}

BpfProgram::BpfProgram(BpfBytecode &bytecode,
                       const char *name,
                       BPFtrace &bpftrace)
    : bytecode_(bytecode),
      bpftrace_(bpftrace),
      name_(name),
      code_(),
      text_offset_(0)
{
}

std::tuple<uint8_t *, uintptr_t> BpfProgram::getCode()
{
  if (code_.empty())
  {
    assemble();
  }
  return std::make_tuple(code_.data(), (uintptr_t)code_.size());
}

void BpfProgram::assemble()
{
  if (!code_.empty())
    return;

  auto &section = bytecode_.at(name_);
  code_.reserve(section.size());
  code_.assign(section.data(), section.data() + section.size());

  relocateInsns();
  relocateMaps();
}

void BpfProgram::relocateInsns()
{
  std::string relsecname = std::string(".rel") + name_;
  if (bytecode_.find(relsecname) != bytecode_.end())
  {
    // There's a relocation section for our program.
    //
    // Relocation support is incomplete, only ld_imm64 + R_BPF_64_64 is
    // supported to make pointers to subprog callbacks possible.
    //
    // In practice, we append the entire .text section and relocate against it.

    auto seciter = bytecode_.find(".text");
    if (seciter == bytecode_.end())
      throw std::logic_error(
          "Relocation section present but no .text, this is unsupported");
    auto &text = seciter->second;
    auto &relsec = bytecode_.find(relsecname)->second;
    auto &symtab = bytecode_.find(".symtab")->second;

    // Step 1: append .text
    text_offset_ = code_.size();
    code_.resize(code_.size() + text.size());
    std::memcpy(code_.data() + text_offset_, text.data(), text.size());

    auto *insns = reinterpret_cast<struct bpf_insn *>(code_.data());

    // Step 2: relocate our program
    for (auto *ptr = relsec.data(); ptr < relsec.data() + relsec.size();
         ptr += sizeof(Elf64_Rel))
    {
      auto *rel = reinterpret_cast<Elf64_Rel *>(ptr);
      uint32_t reltype = rel->r_info & 0xFFFFFFFF;
      uint32_t relsym = rel->r_info >> 32;

      if (reltype != R_BPF_64_64)
        throw std::invalid_argument("Unsupported relocation type");

      // Our program is at the beginning, so the offset is correct.
      auto rel_offset = rel->r_offset;
      auto insn_offset = rel_offset / sizeof(bpf_insn);
      auto *insn = &insns[insn_offset];
      if (insn->code != (BPF_LD | BPF_DW | BPF_IMM))
      {
        LOG(ERROR) << "Cannot relocate insn code " << insn->code << " ld "
                   << (insn->code & BPF_LD) << " dw " << (insn->code & BPF_DW)
                   << " imm " << (insn->code & BPF_IMM);
        throw std::invalid_argument("Unsupported relocated instruction");
      }

      auto *sym = &reinterpret_cast<Elf64_Sym *>(symtab.data())[relsym];
      auto symtype = ELF64_ST_TYPE(sym->st_info);
      if (symtype != STT_FUNC && symtype != STT_SECTION)
      {
        LOG(ERROR) << "Relocation in " << relsecname << " type " << reltype
                   << " sym " << relsym << " type " << symtype;
        throw std::invalid_argument(
            "Unsupported symbol type referenced in relocation");
      }

      // Assume sym->st_shndx corresponds to .text, therefore symbol value is
      // an offset from text_offset_.
      //
      // Relocate via direct instruction manipulation instead of the
      // relocation entry for readibility purposes.
      //
      // This is a hack. We shouldn't do this. However, we don't actually have
      // the ELF section table to determine if the relocation actually refers
      // to .text
      auto target_insn = (text_offset_ + sym->st_value + insn->imm) /
                         sizeof(struct bpf_insn);
      insn->src_reg = BPF_PSEUDO_FUNC;
      insn->imm = (target_insn - insn_offset - 1); // jump offset
    }

    // Step 3: relocate .text, if necessary. TODO.
    bool need_text_rels = text_offset_ > 0 &&
                          bytecode_.find(".rel.text") != bytecode_.end();
    if (need_text_rels)
      throw std::logic_error("Relocations in .text are not implemented yet");
  }

  // Step 4: deal with bpf_func_infos.
  relocateFuncInfos();
}

// Assumed structure:
//
// code_[0..program_offset) - .text
// code_[program_offset..code_.size()) - program
void BpfProgram::relocateFuncInfos()
{
  const auto *btfsec = bytecode_.find(".BTF")->second.data();
  auto *btfhdr = reinterpret_cast<const struct btf_header *>(btfsec);
  auto *btfstr = btfsec + btfhdr->hdr_len + btfhdr->str_off;

  const auto *btfextsec = bytecode_.find(".BTF.ext")->second.data();

  auto *exthdr = reinterpret_cast<const struct btf_ext_header *>(btfextsec);
  auto *exthdr_end = btfextsec + exthdr->hdr_len;
  auto *func_info_end = exthdr_end + exthdr->func_info_len;

  auto *ptr = exthdr_end + exthdr->func_info_off;

  auto func_info_rec_size = *reinterpret_cast<const __u32 *>(ptr);
  if (sizeof(bpf_func_info) > func_info_rec_size)
    throw std::invalid_argument("Unsupported bpf_func_info size");
  ptr += sizeof(__u32);

  // We need to find the bpf_ext_info_secs for our program section and,
  // optionally, for .text. They're likely not in the order we need them in,
  // so find them first, then copy things over, so we keep the invariant that
  // the first func_info is for the function at offset 0.
  //
  const btf_ext_info_sec *text_funcinfo_sec = nullptr;
  const btf_ext_info_sec *prog_funcinfo_sec = nullptr;

  while (ptr < func_info_end)
  {
    auto *info_sec = reinterpret_cast<const struct btf_ext_info_sec *>(ptr);
    auto *name = reinterpret_cast<const char *>(btfstr +
                                                info_sec->sec_name_off);
    if (text_offset_ > 0 && strcmp(name, ".text") == 0)
      text_funcinfo_sec = reinterpret_cast<const btf_ext_info_sec *>(ptr);
    else if (strcmp(name, name_.c_str()) == 0)
      prog_funcinfo_sec = reinterpret_cast<const btf_ext_info_sec *>(ptr);

    ptr += sizeof(struct btf_ext_info_sec) +
           (uintptr_t)info_sec->num_info * func_info_rec_size;
  }

  if (prog_funcinfo_sec == nullptr)
    throw std::invalid_argument("Missing btf_ext_info_sec for program section");
  if (text_offset_ > 0 && text_funcinfo_sec == nullptr)
    throw std::invalid_argument("Missing btf_ext_info_sec for .text section");

  appendFileFuncInfos(prog_funcinfo_sec, func_info_rec_size, 0);
  if (text_offset_ > 0)
    appendFileFuncInfos(text_funcinfo_sec,
                        func_info_rec_size,
                        text_offset_ / sizeof(bpf_insn));
}

// Copy all ELF func_infos from src and convert them to kernel
// bpf_func_infos, adding insn_off to the final value.
void BpfProgram::appendFileFuncInfos(const struct btf_ext_info_sec *src,
                                     size_t func_info_rec_size,
                                     size_t insn_off)
{
  auto *ptr = reinterpret_cast<const uint8_t *>(src);
  auto *hdr_end = ptr + sizeof(struct btf_ext_info_sec);
  auto cnt = src->num_info;

  size_t dstoff = func_infos_.size();
  func_infos_.resize(dstoff + cnt * sizeof(struct bpf_func_info));

  ptr = hdr_end;
  while (ptr < hdr_end + cnt * func_info_rec_size)
  {
    auto *src_info = reinterpret_cast<const struct bpf_func_info *>(ptr);
    auto *dst_info = reinterpret_cast<struct bpf_func_info *>(
        func_infos_.data() + dstoff);

    dst_info->type_id = src_info->type_id;
    dst_info->insn_off = (src_info->insn_off / sizeof(struct bpf_insn)) +
                         insn_off;

    dstoff += sizeof(struct bpf_func_info);
    ptr += func_info_rec_size;
  }
}

void BpfProgram::relocateMaps()
{
  struct bpf_insn *insns = reinterpret_cast<struct bpf_insn *>(code_.data());
  for (uintptr_t i = 0; i < code_.size() / sizeof(struct bpf_insn); ++i)
  {
    struct bpf_insn *insn = &insns[i];

    // Relocate mapid -> mapfd
    //
    // This relocation keeps codegen independent of runtime state (such as FD
    // numbers). This helps make codegen tests more reliable and enables
    // features such as AOT compilation.
    if (insn->code == BPF_DW && (insn->src_reg == BPF_PSEUDO_MAP_FD ||
                                 insn->src_reg == BPF_PSEUDO_MAP_VALUE))
    {
      auto mapid = insn->imm;
      auto map = bpftrace_.maps[mapid];
      if (map)
        insn->imm = static_cast<int32_t>((*map)->mapfd_);
      else
        throw std::runtime_error(std::string("Unknown map id ") +
                                 std::to_string(mapid));

      ++i; // ldimm64 is 2 insns wide
    }
  }
}

} // namespace bpftrace
