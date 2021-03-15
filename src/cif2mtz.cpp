// Copyright 2019 Global Phasing Ltd.
//
// convert SF-mmCIF to MTZ

#include <cstdio>             // for fprintf
#include <cstdlib>            // for exit
#include <memory>             // for unique_ptr
#ifndef GEMMI_ALL_IN_ONE
# define GEMMI_WRITE_IMPLEMENTATION 1
#endif
#include <gemmi/cifdoc.hpp>   // for Loop, as_int, ...
#include <gemmi/fail.hpp>     // for fail
#include <gemmi/mtz.hpp>      // for Mtz
#include <gemmi/numb.hpp>     // for as_number
#include <gemmi/refln.hpp>    // for ReflnBlock
#include <gemmi/read_cif.hpp> // for read_cif_gz

#define GEMMI_PROG cif2mtz
#include "options.h"

namespace {

namespace cif = gemmi::cif;
using std::fprintf;

enum OptionIndex { BlockName=4, Dir, Spec, PrintSpec, Title, History, Unmerged };

const option::Descriptor Usage[] = {
  { NoOp, 0, "", "", Arg::None,
    "Usage:"
    "\n  " EXE_NAME " [options] CIF_FILE MTZ_FILE"
    "\n  " EXE_NAME " [options] CIF_FILE --dir=DIRECTORY"
    "\nOptions:"},
  CommonUsage[Help],
  CommonUsage[Version],
  CommonUsage[Verbose],
  { BlockName, 0, "b", "block", Arg::Required,
    "  -b NAME, --block=NAME  \tmmCIF block to convert." },
  { Dir, 0, "d", "dir", Arg::Required,
    "  -d DIR, --dir=NAME  \tOutput directory." },
  { Spec, 0, "", "spec", Arg::Required,
    "  --spec=FILE  \tConversion spec." },
  { PrintSpec, 0, "", "print-spec", Arg::None,
    "  --print-spec  \tPrint default spec and exit." },
  { Title, 0, "", "title", Arg::Required,
    "  --title  \tMTZ title." },
  { History, 0, "-H", "history", Arg::Required,
    "  -H LINE, --history=LINE  \tAdd a history line." },
  { Unmerged, 0, "u", "unmerged", Arg::None,
    "  -u, --unmerged  \tWrite unmerged MTZ file(s)." },
  { NoOp, 0, "", "", Arg::None,
    "\nFirst variant: converts the first block of CIF_FILE, or the block"
    "\nspecified with --block=NAME, to MTZ file with given name."
    "\n\nSecond variant: converts each block of CIF_FILE to one MTZ file"
    "\n(block-name.mtz) in the specified DIRECTORY."
    "\n\nIf CIF_FILE is -, the input is read from stdin."
  },
  { 0, 0, 0, 0, 0, 0 }
};

static const char* default_spec[] = {
  "index_h H H 0",
  "index_k K H 0",
  "index_l L H 0",
  "pdbx_r_free_flag FreeR_flag I 0",
  "status FreeR_flag s 0", // s is a special flag
  "intensity_meas I J 1",
  "intensity_net I J 1",
  "intensity_sigma SIGI Q 1",
  "pdbx_I_plus I(+) K 1",
  "pdbx_I_plus_sigma SIGI(+) M 1",
  "pdbx_I_minus I(-) K 1",
  "pdbx_I_minus_sigma SIGI(-) M 1",
  "F_meas_au FP F 1",
  "F_meas_sigma_au SIGFP Q 1",
  "pdbx_F_plus F(+) G 1",
  "pdbx_F_plus_sigma SIGF(+) L 1",
  "pdbx_F_minus F(-) G 1",
  "pdbx_F_minus_sigma SIGF(-) L 1",
  "pdbx_anom_difference DP D 1",
  "pdbx_anom_difference_sigma SIGDP Q 1",
  "F_calc FC F 1",
  "phase_calc PHIC P 1",
  "fom FOM W 1",
  "weight FOM W 1",
  "pdbx_HL_A_iso HLA A 1",
  "pdbx_HL_B_iso HLB A 1",
  "pdbx_HL_C_iso HLC A 1",
  "pdbx_HL_D_iso HLD A 1",
  "pdbx_FWT FWT F 1",
  "pdbx_PHWT PHWT P 1",
  "pdbx_DELFWT DELFWT F 1",
  "pdbx_DELPHWT DELPHWT P 1",
};

struct CifToMtz {

  struct Entry {
    std::string refln_tag;
    std::string col_label;
    char col_type;
    int dataset_id;
  };

  // Alternative mmCIF tags for the same MTZ label should be consecutive
  std::vector<Entry> spec_entries;
  bool verbose = false;
  bool force_unmerged = false;
  const char* title = nullptr;
  std::vector<std::string> history;

  void convert_block_to_mtz(const gemmi::ReflnBlock& rb, const std::string& mtz_path) {
    gemmi::Mtz mtz;
    if (title)
      mtz.title = title;
    if (!history.empty()) {
      mtz.history.reserve(mtz.history.size() + history.size());
      mtz.history.insert(mtz.history.end(), history.begin(), history.end());
    }
    mtz.cell = rb.cell;
    mtz.spacegroup = rb.spacegroup;
    mtz.add_dataset("HKL_base");
    mtz.add_dataset("unknown").wavelength = rb.wavelength;
    const cif::Loop* loop = rb.refln_loop ? rb.refln_loop : rb.diffrn_refln_loop;
    if (!loop)
      gemmi::fail("_refln category not found in mmCIF block: " + rb.block.name);
    if (verbose)
      fprintf(stderr, "Searching tags with known MTZ equivalents ...\n");
    bool uses_status = false;
    std::vector<int> indices;
    std::string tag = loop->tags[0].substr(0, loop->tags[0].find('.') + 1);
    const size_t len = tag.length();
    bool unmerged = force_unmerged || !rb.refln_loop;
    for (const Entry& entry : spec_entries) {
      tag.replace(len, std::string::npos, entry.refln_tag);
      int index = loop->find_tag(tag);
      if (index != -1) {
        if (!mtz.columns.empty() && mtz.columns.back().label == entry.col_label)
          continue;
        // Some early unmerged depositions such as 1vly have data in _refln
        // and also have _refln.status (always 'o'). We skip it here.
        if (unmerged && entry.col_type == 's')
          continue;
        indices.push_back(index);
        mtz.columns.emplace_back();
        gemmi::Mtz::Column& col = mtz.columns.back();
        col.dataset_id = entry.dataset_id;
        col.type = entry.col_type;
        if (col.type == 's') {
          col.type = 'I';
          uses_status = true;
        }
        col.label = entry.col_label;
        if (verbose)
          fprintf(stderr, "  %s -> %s\n", tag.c_str(), col.label.c_str());
      } else if (entry.col_type == 'H') {
        gemmi::fail("Miller index tag not found: " + tag);
      }
    }
    std::unique_ptr<gemmi::UnmergedHklMover> hkl_mover;
    if (unmerged) {
      if (verbose)
        fprintf(stderr, "Adding columns M/ISYM and BATCH for unmerged data...\n");
      auto col = mtz.columns.emplace(mtz.columns.begin() + 3);
      col->dataset_id = 1;
      col->type = 'Y';
      col->label = "M/ISYM";

      col = mtz.columns.emplace(mtz.columns.begin() + 4);
      col->dataset_id = 1;
      col->type = 'B';
      col->label = "BATCH";

      mtz.batches.emplace_back();
      mtz.batches.back().set_cell(mtz.cell);
      hkl_mover.reset(new gemmi::UnmergedHklMover(mtz.spacegroup));
    }
    for (size_t i = 0; i != mtz.columns.size(); ++i) {
      mtz.columns[i].parent = &mtz;
      mtz.columns[i].idx = i;
    }
    mtz.nreflections = (int) loop->length();
    mtz.data.resize(mtz.columns.size() * mtz.nreflections);
    int k = 0;
    for (size_t i = 0; i < loop->values.size(); i += loop->tags.size()) {
      size_t j = 0;
      if (unmerged) {
        std::array<int, 3> hkl;
        for (int ii = 0; ii != 3; ++ii)
          hkl[ii] = cif::as_int(loop->values[i + indices[ii]]);
        int isym = hkl_mover->move_to_asu(hkl);
        for (; j != 3; ++j)
          mtz.data[k++] = (float) hkl[j];
        mtz.data[k++] = (float) isym;
        mtz.data[k++] = 1.0f; // batch number
      } else {
        for (; j != 3; ++j)
          mtz.data[k++] = (float) cif::as_int(loop->values[i + indices[j]]);
      }
      if (uses_status)
        mtz.data[k++] = status_to_freeflag(loop->values[i + indices[j++]]);
      for (; j != indices.size(); ++j) {
        const std::string& v = loop->values[i + indices[j]];
        if (cif::is_null(v)) {
          mtz.data[k] = (float) NAN;
        } else {
          mtz.data[k] = (float) cif::as_number(v);
          if (std::isnan(mtz.data[k]))
            fprintf(stderr, "Value #%zu in the loop is not a number: %s\n",
                    i + indices[j], v.c_str());
        }
        ++k;
      }
    }
    if (verbose)
      fprintf(stderr, "Writing %s ...\n", mtz_path.c_str());
    try {
      mtz.write_to_file(mtz_path);
    } catch (std::runtime_error& e) {
      fprintf(stderr, "ERROR writing %s: %s\n", mtz_path.c_str(), e.what());
      std::exit(3);
    }
  }

  void add_spec_line(const std::string& line) {
    std::vector<std::string> tokens;
    tokens.reserve(4);
    gemmi::split_str_into_multi(line, " \t\r\n", tokens);
    if (tokens.size() != 4)
      gemmi::fail("line should have 4 words: " + line);
    if (tokens[2].size() != 1 || tokens[3].size() != 1 ||
        (tokens[3][0] != '0' && tokens[3][0] != '1'))
      gemmi::fail("incorrect line: " + line);
    int dataset_id = tokens[3][0] - '0';
    spec_entries.push_back({tokens[0], tokens[1], tokens[2][0], dataset_id});
  }

private:
  static float status_to_freeflag(const std::string& str) {
    char c = str[0];
    if (c == '\'' || c == '"')
      c = str[1];
    if (c == 'o')
      return 1.f;
    if (c == 'f')
      return 0.f;
    return NAN;
  }
};

gemmi::ReflnBlock& get_block_by_name(std::vector<gemmi::ReflnBlock>& rblocks,
                                     const std::string& name) {
  for (gemmi::ReflnBlock& rb : rblocks)
    if (rb.block.name == name)
      return rb;
  gemmi::fail("block not found: " + name);
}

} // anonymous namespace

int GEMMI_MAIN(int argc, char **argv) {
  OptParser p(EXE_NAME);
  p.simple_parse(argc, argv, Usage);
  if (p.options[PrintSpec]) {
    std::printf("# Each line in the spec contains four words:\n"
                "# - tag (without category) from _refln or _diffrn_refln\n"
                "# - MTZ column label\n"
                "# - MTZ column type\n"
                "# - MTZ dataset for the column (must be 0 or 1)\n");
    for (const char* line : default_spec)
      std::printf("%s\n", line);
    return 0;
  }
  bool convert_all = p.options[Dir];
  p.require_positional_args(convert_all ? 1 : 2);

  CifToMtz cif2mtz;

  try {
    if (p.options[Spec]) {
      std::vector<std::string> spec_lines;
      read_spec_file(p.options[Spec].arg, spec_lines);
      cif2mtz.spec_entries.reserve(spec_lines.size());
      for (const std::string& line : spec_lines)
        cif2mtz.add_spec_line(line);
    } else {
      cif2mtz.spec_entries.reserve(sizeof(default_spec) / sizeof(default_spec[0]));
      for (const char* line : default_spec)
        cif2mtz.add_spec_line(line);
    }
  } catch (std::runtime_error& e) {
    std::fprintf(stderr, "Problem with spec: %s\n", e.what());
    return 2;
  }

  cif2mtz.verbose = p.options[Verbose];
  cif2mtz.force_unmerged = p.options[Unmerged];
  if (p.options[Title])
    cif2mtz.title = p.options[Title].arg;
  for (const option::Option* opt = p.options[History]; opt; opt = opt->next())
    cif2mtz.history.push_back(opt->arg);

  const char* cif_path = p.nonOption(0);
  if (cif2mtz.verbose)
    fprintf(stderr, "Reading %s ...\n", cif_path);
  try {
    auto rblocks = gemmi::as_refln_blocks(gemmi::read_cif_gz(cif_path).blocks);
    if (convert_all) {
      bool ok = true;
      for (gemmi::ReflnBlock& rb : rblocks) {
        std::string path = p.options[Dir].arg;
        path += '/';
        path += rb.block.name;
        path += ".mtz";
        try {
          cif2mtz.convert_block_to_mtz(rb, path);
        } catch (std::runtime_error& e) {
          fprintf(stderr, "ERROR: %s\n", e.what());
          ok = false;
        }
      }
      if (!ok)
        return 1;
    } else {
      const char* mtz_path = p.nonOption(1);
      const gemmi::ReflnBlock& rb = p.options[BlockName]
        ? get_block_by_name(rblocks, p.options[BlockName].arg)
        : rblocks.at(0);
      cif2mtz.convert_block_to_mtz(rb, mtz_path);
    }
  } catch (std::runtime_error& e) {
    fprintf(stderr, "ERROR: %s\n", e.what());
    return 1;
  }
  if (cif2mtz.verbose)
    fprintf(stderr, "Done.\n");
  return 0;
}

// vim:sw=2:ts=2:et:path^=../include,../third_party
