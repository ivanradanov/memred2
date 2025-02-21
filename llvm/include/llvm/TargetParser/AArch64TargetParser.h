//===-- AArch64TargetParser - Parser for AArch64 features -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise AArch64 hardware features
// such as FPU/CPU/ARCH and extension names.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_AARCH64TARGETPARSER_H
#define LLVM_TARGETPARSER_AARCH64TARGETPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Bitset.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VersionTuple.h"
#include <array>
#include <vector>

namespace llvm {

class Triple;

namespace AArch64 {

struct ArchInfo;
struct CpuInfo;

// Function Multi Versioning CPU features. They must be kept in sync with
// compiler-rt enum CPUFeatures in lib/builtins/cpu_model/aarch64.c with
// FEAT_MAX as sentinel.
enum CPUFeatures {
  FEAT_RNG,
  FEAT_FLAGM,
  FEAT_FLAGM2,
  FEAT_FP16FML,
  FEAT_DOTPROD,
  FEAT_SM4,
  FEAT_RDM,
  FEAT_LSE,
  FEAT_FP,
  FEAT_SIMD,
  FEAT_CRC,
  FEAT_SHA1,
  FEAT_SHA2,
  FEAT_SHA3,
  FEAT_AES,
  FEAT_PMULL,
  FEAT_FP16,
  FEAT_DIT,
  FEAT_DPB,
  FEAT_DPB2,
  FEAT_JSCVT,
  FEAT_FCMA,
  FEAT_RCPC,
  FEAT_RCPC2,
  FEAT_FRINTTS,
  FEAT_DGH,
  FEAT_I8MM,
  FEAT_BF16,
  FEAT_EBF16,
  FEAT_RPRES,
  FEAT_SVE,
  FEAT_SVE_BF16,
  FEAT_SVE_EBF16,
  FEAT_SVE_I8MM,
  FEAT_SVE_F32MM,
  FEAT_SVE_F64MM,
  FEAT_SVE2,
  FEAT_SVE_AES,
  FEAT_SVE_PMULL128,
  FEAT_SVE_BITPERM,
  FEAT_SVE_SHA3,
  FEAT_SVE_SM4,
  FEAT_SME,
  FEAT_MEMTAG,
  FEAT_MEMTAG2,
  FEAT_MEMTAG3,
  FEAT_SB,
  FEAT_PREDRES,
  FEAT_SSBS,
  FEAT_SSBS2,
  FEAT_BTI,
  FEAT_LS64,
  FEAT_LS64_V,
  FEAT_LS64_ACCDATA,
  FEAT_WFXT,
  FEAT_SME_F64,
  FEAT_SME_I64,
  FEAT_SME2,
  FEAT_RCPC3,
  FEAT_MOPS,
  FEAT_MAX,
  FEAT_EXT = 62,
  FEAT_INIT
};

static_assert(FEAT_MAX < 62,
              "Number of features in CPUFeatures are limited to 62 entries");

// Each ArchExtKind correponds directly to a possible -target-feature.
#define EMIT_ARCHEXTKIND_ENUM
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

using ExtensionBitset = Bitset<AEK_NUM_EXTENSIONS>;

// Represents an extension that can be enabled with -march=<arch>+<extension>.
// Typically these correspond to Arm Architecture extensions, unlike
// SubtargetFeature which may represent either an actual extension or some
// internal LLVM property.
struct ExtensionInfo {
  StringRef Name;              // Human readable name, e.g. "profile".
  ArchExtKind ID;              // Corresponding to the ArchExtKind, this
                               // extensions representation in the bitfield.
  StringRef Feature;           // -mattr enable string, e.g. "+spe"
  StringRef NegFeature;        // -mattr disable string, e.g. "-spe"
  CPUFeatures CPUFeature;      // Function Multi Versioning (FMV) bitfield value
                               // set in __aarch64_cpu_features
  StringRef DependentFeatures; // FMV enabled features string,
                               // e.g. "+dotprod,+fp-armv8,+neon"
  unsigned FmvPriority;        // FMV feature priority
  static constexpr unsigned MaxFMVPriority =
      1000; // Maximum priority for FMV feature
};

#define EMIT_EXTENSIONS
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

struct ExtensionSet {
  // Set of extensions which are currently enabled.
  ExtensionBitset Enabled;
  // Set of extensions which have been enabled or disabled at any point. Used
  // to avoid cluttering the cc1 command-line with lots of unneeded features.
  ExtensionBitset Touched;
  // Base architecture version, which we need to know because some feature
  // dependencies change depending on this.
  const ArchInfo *BaseArch;

  ExtensionSet() : Enabled(), Touched(), BaseArch(nullptr) {}

  // Enable the given architecture extension, and any other extensions it
  // depends on. Does not change the base architecture, or follow dependencies
  // between features which are only related by required arcitecture versions.
  void enable(ArchExtKind E);

  // Disable the given architecture extension, and any other extensions which
  // depend on it. Does not change the base architecture, or follow
  // dependencies between features which are only related by required
  // arcitecture versions.
  void disable(ArchExtKind E);

  // Add default extensions for the given CPU. Records the base architecture,
  // to later resolve dependencies which depend on it.
  void addCPUDefaults(const CpuInfo &CPU);

  // Add default extensions for the given architecture version. Records the
  // base architecture, to later resolve dependencies which depend on it.
  void addArchDefaults(const ArchInfo &Arch);

  // Add or remove a feature based on a modifier string. The string must be of
  // the form "<name>" to enable a feature or "no<name>" to disable it. This
  // will also enable or disable any features as required by the dependencies
  // between them.
  bool parseModifier(StringRef Modifier);

  // Convert the set of enabled extension to an LLVM feature list, appending
  // them to Features.
  void toLLVMFeatureList(std::vector<StringRef> &Features) const;
};

// Represents a dependency between two architecture extensions. Later is the
// feature which was added to the architecture after Earlier, and expands the
// functionality provided by it. If Later is enabled, then Earlier will also be
// enabled. If Earlier is disabled, then Later will also be disabled.
struct ExtensionDependency {
  ArchExtKind Earlier;
  ArchExtKind Later;
};

// clang-format off
// Each entry here is a link in the dependency chain starting from the
// extension that was added to the architecture first.
inline constexpr ExtensionDependency ExtensionDependencies[] = {
  {AEK_FP, AEK_FP16},
  {AEK_FP, AEK_SIMD},
  {AEK_FP, AEK_JSCVT},
  {AEK_FP, AEK_FP8},
  {AEK_SIMD, AEK_CRYPTO},
  {AEK_SIMD, AEK_AES},
  {AEK_SIMD, AEK_SHA2},
  {AEK_SIMD, AEK_SHA3},
  {AEK_SIMD, AEK_SM4},
  {AEK_SIMD, AEK_RDM},
  {AEK_SIMD, AEK_DOTPROD},
  {AEK_SIMD, AEK_FCMA},
  {AEK_FP16, AEK_FP16FML},
  {AEK_FP16, AEK_SVE},
  {AEK_BF16, AEK_SME},
  {AEK_BF16, AEK_B16B16},
  {AEK_SVE, AEK_SVE2},
  {AEK_SVE, AEK_F32MM},
  {AEK_SVE, AEK_F64MM},
  {AEK_SVE2, AEK_SVE2P1},
  {AEK_SVE2, AEK_SVE2BITPERM},
  {AEK_SVE2, AEK_SVE2AES},
  {AEK_SVE2, AEK_SVE2SHA3},
  {AEK_SVE2, AEK_SVE2SM4},
  {AEK_SVE2, AEK_SMEFA64},
  {AEK_SVE2, AEK_SMEFA64},
  {AEK_SME, AEK_SME2},
  {AEK_SME, AEK_SMEF16F16},
  {AEK_SME, AEK_SMEF64F64},
  {AEK_SME, AEK_SMEI16I64},
  {AEK_SME, AEK_SMEFA64},
  {AEK_SME2, AEK_SME2P1},
  {AEK_SME2, AEK_SSVE_FP8FMA},
  {AEK_SME2, AEK_SSVE_FP8DOT2},
  {AEK_SME2, AEK_SSVE_FP8DOT4},
  {AEK_SME2, AEK_SMEF8F16},
  {AEK_SME2, AEK_SMEF8F32},
  {AEK_FP8, AEK_SMEF8F16},
  {AEK_FP8, AEK_SMEF8F32},
  {AEK_LSE, AEK_LSE128},
  {AEK_PREDRES, AEK_SPECRES2},
  {AEK_RAS, AEK_RASV2},
  {AEK_RCPC, AEK_RCPC3},
};
// clang-format on

enum ArchProfile { AProfile = 'A', RProfile = 'R', InvalidProfile = '?' };

// Information about a specific architecture, e.g. V8.1-A
struct ArchInfo {
  VersionTuple Version;  // Architecture version, major + minor.
  ArchProfile Profile;   // Architecuture profile
  StringRef Name;        // Human readable name, e.g. "armv8.1-a"
  StringRef ArchFeature; // Command line feature flag, e.g. +v8a
  AArch64::ExtensionBitset
      DefaultExts; // bitfield of default extensions ArchExtKind

  bool operator==(const ArchInfo &Other) const {
    return this->Name == Other.Name;
  }
  bool operator!=(const ArchInfo &Other) const {
    return this->Name != Other.Name;
  }

  // Defines the following partial order, indicating when an architecture is
  // a superset of another:
  //
  //   v9.5a > v9.4a > v9.3a > v9.2a > v9.1a > v9a;
  //             v       v       v       v       v
  //           v8.9a > v8.8a > v8.7a > v8.6a > v8.5a > v8.4a > ... > v8a;
  //
  // v8r has no relation to anything. This is used to determine which
  // features to enable for a given architecture. See
  // AArch64TargetInfo::setFeatureEnabled.
  bool implies(const ArchInfo &Other) const {
    if (this->Profile != Other.Profile)
      return false; // ARMV8R
    if (this->Version.getMajor() == Other.Version.getMajor()) {
      return this->Version > Other.Version;
    }
    if (this->Version.getMajor() == 9 && Other.Version.getMajor() == 8) {
      assert(this->Version.getMinor() && Other.Version.getMinor() &&
             "AArch64::ArchInfo should have a minor version.");
      return this->Version.getMinor().value_or(0) + 5 >=
             Other.Version.getMinor().value_or(0);
    }
    return false;
  }

  // True if this architecture is a superset of Other (including being equal to
  // it).
  bool is_superset(const ArchInfo &Other) const {
    return (*this == Other) || implies(Other);
  }

  // Return ArchFeature without the leading "+".
  StringRef getSubArch() const { return ArchFeature.substr(1); }

  // Search for ArchInfo by SubArch name
  static std::optional<ArchInfo> findBySubArch(StringRef SubArch);
};

// clang-format off
inline constexpr ArchInfo ARMV8A    = { VersionTuple{8, 0}, AProfile, "armv8-a", "+v8a", (
                                        AArch64::ExtensionBitset({AArch64::AEK_FP, AArch64::AEK_SIMD})), };
inline constexpr ArchInfo ARMV8_1A  = { VersionTuple{8, 1}, AProfile, "armv8.1-a", "+v8.1a", (ARMV8A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_CRC, AArch64::AEK_LSE, AArch64::AEK_RDM}))};
inline constexpr ArchInfo ARMV8_2A  = { VersionTuple{8, 2}, AProfile, "armv8.2-a", "+v8.2a", (ARMV8_1A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_RAS}))};
inline constexpr ArchInfo ARMV8_3A  = { VersionTuple{8, 3}, AProfile, "armv8.3-a", "+v8.3a", (ARMV8_2A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_FCMA, AArch64::AEK_JSCVT, AArch64::AEK_PAUTH, AArch64::AEK_RCPC}))};
inline constexpr ArchInfo ARMV8_4A  = { VersionTuple{8, 4}, AProfile, "armv8.4-a", "+v8.4a", (ARMV8_3A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_DOTPROD}))};
inline constexpr ArchInfo ARMV8_5A  = { VersionTuple{8, 5}, AProfile, "armv8.5-a", "+v8.5a", (ARMV8_4A.DefaultExts)};
inline constexpr ArchInfo ARMV8_6A  = { VersionTuple{8, 6}, AProfile, "armv8.6-a", "+v8.6a", (ARMV8_5A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_BF16, AArch64::AEK_I8MM}))};
inline constexpr ArchInfo ARMV8_7A  = { VersionTuple{8, 7}, AProfile, "armv8.7-a", "+v8.7a", (ARMV8_6A.DefaultExts)};
inline constexpr ArchInfo ARMV8_8A  = { VersionTuple{8, 8}, AProfile, "armv8.8-a", "+v8.8a", (ARMV8_7A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_MOPS, AArch64::AEK_HBC}))};
inline constexpr ArchInfo ARMV8_9A  = { VersionTuple{8, 9}, AProfile, "armv8.9-a", "+v8.9a", (ARMV8_8A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_SPECRES2, AArch64::AEK_CSSC, AArch64::AEK_RASV2}))};
inline constexpr ArchInfo ARMV9A    = { VersionTuple{9, 0}, AProfile, "armv9-a", "+v9a", (ARMV8_5A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_FP16, AArch64::AEK_SVE, AArch64::AEK_SVE2}))};
inline constexpr ArchInfo ARMV9_1A  = { VersionTuple{9, 1}, AProfile, "armv9.1-a", "+v9.1a", (ARMV9A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_BF16, AArch64::AEK_I8MM}))};
inline constexpr ArchInfo ARMV9_2A  = { VersionTuple{9, 2}, AProfile, "armv9.2-a", "+v9.2a", (ARMV9_1A.DefaultExts)};
inline constexpr ArchInfo ARMV9_3A  = { VersionTuple{9, 3}, AProfile, "armv9.3-a", "+v9.3a", (ARMV9_2A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_MOPS, AArch64::AEK_HBC}))};
inline constexpr ArchInfo ARMV9_4A  = { VersionTuple{9, 4}, AProfile, "armv9.4-a", "+v9.4a", (ARMV9_3A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_SPECRES2, AArch64::AEK_CSSC, AArch64::AEK_RASV2}))};
inline constexpr ArchInfo ARMV9_5A  = { VersionTuple{9, 5}, AProfile, "armv9.5-a", "+v9.5a", (ARMV9_4A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_CPA}))};
// For v8-R, we do not enable crypto and align with GCC that enables a more minimal set of optional architecture extensions.
inline constexpr ArchInfo ARMV8R    = { VersionTuple{8, 0}, RProfile, "armv8-r", "+v8r", (ARMV8_5A.DefaultExts |
                                        AArch64::ExtensionBitset({AArch64::AEK_SSBS,
                                        AArch64::AEK_FP16, AArch64::AEK_FP16FML, AArch64::AEK_SB}).flip(AArch64::AEK_LSE))};
// clang-format on

// The set of all architectures
static constexpr std::array<const ArchInfo *, 17> ArchInfos = {
    &ARMV8A,   &ARMV8_1A, &ARMV8_2A, &ARMV8_3A, &ARMV8_4A, &ARMV8_5A,
    &ARMV8_6A, &ARMV8_7A, &ARMV8_8A, &ARMV8_9A, &ARMV9A,   &ARMV9_1A,
    &ARMV9_2A, &ARMV9_3A, &ARMV9_4A, &ARMV9_5A, &ARMV8R,
};

// Details of a specific CPU.
struct CpuInfo {
  StringRef Name; // Name, as written for -mcpu.
  const ArchInfo &Arch;
  AArch64::ExtensionBitset
      DefaultExtensions; // Default extensions for this CPU. These will be
                         // ORd with the architecture defaults.

  AArch64::ExtensionBitset getImpliedExtensions() const {
    AArch64::ExtensionBitset ImpliedExts;
    ImpliedExts |= DefaultExtensions;
    ImpliedExts |= Arch.DefaultExts;
    return ImpliedExts;
  }
};

inline constexpr CpuInfo CpuInfos[] = {
    {"cortex-a34", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"cortex-a35", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"cortex-a53", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"cortex-a55", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_FP16, AArch64::AEK_DOTPROD,
                               AArch64::AEK_RCPC})},
    {"cortex-a510", ARMV9A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_BF16, AArch64::AEK_I8MM, AArch64::AEK_SB,
          AArch64::AEK_PAUTH, AArch64::AEK_MTE, AArch64::AEK_SSBS,
          AArch64::AEK_SVE, AArch64::AEK_SVE2, AArch64::AEK_SVE2BITPERM,
          AArch64::AEK_FP16FML})},
    {"cortex-a520", ARMV9_2A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_SB, AArch64::AEK_SSBS, AArch64::AEK_MTE,
          AArch64::AEK_FP16FML, AArch64::AEK_PAUTH, AArch64::AEK_SVE2BITPERM,
          AArch64::AEK_FLAGM, AArch64::AEK_PERFMON, AArch64::AEK_PREDRES})},
    {"cortex-a520ae", ARMV9_2A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_SB, AArch64::AEK_SSBS, AArch64::AEK_MTE,
          AArch64::AEK_FP16FML, AArch64::AEK_PAUTH, AArch64::AEK_SVE2BITPERM,
          AArch64::AEK_FLAGM, AArch64::AEK_PERFMON, AArch64::AEK_PREDRES})},
    {"cortex-a57", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"cortex-a65", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_DOTPROD, AArch64::AEK_FP16,
                               AArch64::AEK_RCPC, AArch64::AEK_SSBS})},
    {"cortex-a65ae", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_DOTPROD, AArch64::AEK_FP16,
                               AArch64::AEK_RCPC, AArch64::AEK_SSBS})},
    {"cortex-a72", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"cortex-a73", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"cortex-a75", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_FP16, AArch64::AEK_DOTPROD,
                               AArch64::AEK_RCPC})},
    {"cortex-a76", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_FP16, AArch64::AEK_DOTPROD,
                               AArch64::AEK_RCPC, AArch64::AEK_SSBS})},
    {"cortex-a76ae", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_FP16, AArch64::AEK_DOTPROD,
                               AArch64::AEK_RCPC, AArch64::AEK_SSBS})},
    {"cortex-a77", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_FP16, AArch64::AEK_RCPC,
                               AArch64::AEK_DOTPROD, AArch64::AEK_SSBS})},
    {"cortex-a78", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_FP16, AArch64::AEK_DOTPROD,
                               AArch64::AEK_RCPC, AArch64::AEK_SSBS,
                               AArch64::AEK_PROFILE})},
    {"cortex-a78ae", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_FP16, AArch64::AEK_DOTPROD,
                               AArch64::AEK_RCPC, AArch64::AEK_SSBS,
                               AArch64::AEK_PROFILE})},
    {"cortex-a78c", ARMV8_2A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_FP16,
          AArch64::AEK_DOTPROD, AArch64::AEK_RCPC, AArch64::AEK_SSBS,
          AArch64::AEK_PROFILE, AArch64::AEK_FLAGM, AArch64::AEK_PAUTH})},
    {"cortex-a710", ARMV9A,
     AArch64::ExtensionBitset({AArch64::AEK_MTE, AArch64::AEK_PAUTH,
                               AArch64::AEK_FLAGM, AArch64::AEK_SB,
                               AArch64::AEK_I8MM, AArch64::AEK_FP16FML,
                               AArch64::AEK_SVE, AArch64::AEK_SVE2,
                               AArch64::AEK_SVE2BITPERM, AArch64::AEK_BF16})},
    {"cortex-a715", ARMV9A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_SB, AArch64::AEK_SSBS, AArch64::AEK_MTE,
          AArch64::AEK_FP16, AArch64::AEK_FP16FML, AArch64::AEK_PAUTH,
          AArch64::AEK_I8MM, AArch64::AEK_PREDRES, AArch64::AEK_PERFMON,
          AArch64::AEK_PROFILE, AArch64::AEK_SVE, AArch64::AEK_SVE2BITPERM,
          AArch64::AEK_BF16, AArch64::AEK_FLAGM})},
    {"cortex-a720", ARMV9_2A,
     AArch64::ExtensionBitset({AArch64::AEK_SB, AArch64::AEK_SSBS,
                               AArch64::AEK_MTE, AArch64::AEK_FP16FML,
                               AArch64::AEK_PAUTH, AArch64::AEK_SVE2BITPERM,
                               AArch64::AEK_FLAGM, AArch64::AEK_PERFMON,
                               AArch64::AEK_PREDRES, AArch64::AEK_PROFILE})},
    {"cortex-a720ae", ARMV9_2A,
     AArch64::ExtensionBitset({AArch64::AEK_SB, AArch64::AEK_SSBS,
                               AArch64::AEK_MTE, AArch64::AEK_FP16FML,
                               AArch64::AEK_PAUTH, AArch64::AEK_SVE2BITPERM,
                               AArch64::AEK_FLAGM, AArch64::AEK_PERFMON,
                               AArch64::AEK_PREDRES, AArch64::AEK_PROFILE})},
    {"cortex-r82", ARMV8R,
     AArch64::ExtensionBitset({AArch64::AEK_LSE, AArch64::AEK_FLAGM,
                               AArch64::AEK_PERFMON, AArch64::AEK_PREDRES})},
    {"cortex-r82ae", ARMV8R,
     AArch64::ExtensionBitset({AArch64::AEK_LSE, AArch64::AEK_FLAGM,
                               AArch64::AEK_PERFMON, AArch64::AEK_PREDRES})},
    {"cortex-x1", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_FP16, AArch64::AEK_DOTPROD,
                               AArch64::AEK_RCPC, AArch64::AEK_SSBS,
                               AArch64::AEK_PROFILE})},
    {"cortex-x1c", ARMV8_2A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_FP16,
          AArch64::AEK_DOTPROD, AArch64::AEK_RCPC, AArch64::AEK_SSBS,
          AArch64::AEK_PAUTH, AArch64::AEK_PROFILE, AArch64::AEK_FLAGM})},
    {"cortex-x2", ARMV9A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_MTE, AArch64::AEK_BF16, AArch64::AEK_I8MM,
          AArch64::AEK_PAUTH, AArch64::AEK_SSBS, AArch64::AEK_SB,
          AArch64::AEK_SVE, AArch64::AEK_SVE2, AArch64::AEK_SVE2BITPERM,
          AArch64::AEK_FP16FML, AArch64::AEK_FLAGM})},
    {"cortex-x3", ARMV9A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_SVE, AArch64::AEK_PERFMON, AArch64::AEK_PROFILE,
          AArch64::AEK_BF16, AArch64::AEK_I8MM, AArch64::AEK_MTE,
          AArch64::AEK_SVE2BITPERM, AArch64::AEK_SB, AArch64::AEK_PAUTH,
          AArch64::AEK_FP16, AArch64::AEK_FP16FML, AArch64::AEK_PREDRES,
          AArch64::AEK_FLAGM, AArch64::AEK_SSBS})},
    {"cortex-x4", ARMV9_2A,
     AArch64::ExtensionBitset({AArch64::AEK_SB, AArch64::AEK_SSBS,
                               AArch64::AEK_MTE, AArch64::AEK_FP16FML,
                               AArch64::AEK_PAUTH, AArch64::AEK_SVE2BITPERM,
                               AArch64::AEK_FLAGM, AArch64::AEK_PERFMON,
                               AArch64::AEK_PREDRES, AArch64::AEK_PROFILE})},
    {"neoverse-e1", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_DOTPROD, AArch64::AEK_FP16,
                               AArch64::AEK_RCPC, AArch64::AEK_SSBS})},
    {"neoverse-n1", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_DOTPROD, AArch64::AEK_FP16,
                               AArch64::AEK_PROFILE, AArch64::AEK_RCPC,
                               AArch64::AEK_SSBS})},
    {"neoverse-n2", ARMV9A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_BF16, AArch64::AEK_DOTPROD, AArch64::AEK_FP16,
          AArch64::AEK_FP16FML, AArch64::AEK_I8MM, AArch64::AEK_MTE,
          AArch64::AEK_SB, AArch64::AEK_SSBS, AArch64::AEK_SVE,
          AArch64::AEK_SVE2, AArch64::AEK_SVE2BITPERM})},
    {"neoverse-n3", ARMV9_2A,
     AArch64::ExtensionBitset({AArch64::AEK_MTE, AArch64::AEK_SSBS,
                               AArch64::AEK_SB, AArch64::AEK_PREDRES,
                               AArch64::AEK_FP16FML, AArch64::AEK_PAUTH,
                               AArch64::AEK_FLAGM, AArch64::AEK_PERFMON,
                               AArch64::AEK_RAND, AArch64::AEK_SVE2BITPERM,
                               AArch64::AEK_PROFILE, AArch64::AEK_PERFMON})},
    {"neoverse-512tvb", ARMV8_4A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_SHA3,
          AArch64::AEK_SM4, AArch64::AEK_SVE, AArch64::AEK_SSBS,
          AArch64::AEK_FP16, AArch64::AEK_BF16, AArch64::AEK_DOTPROD,
          AArch64::AEK_PROFILE, AArch64::AEK_RAND, AArch64::AEK_FP16FML,
          AArch64::AEK_I8MM})},
    {"neoverse-v1", ARMV8_4A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_SHA3,
          AArch64::AEK_SM4, AArch64::AEK_SVE, AArch64::AEK_SSBS,
          AArch64::AEK_FP16, AArch64::AEK_BF16, AArch64::AEK_DOTPROD,
          AArch64::AEK_PROFILE, AArch64::AEK_RAND, AArch64::AEK_FP16FML,
          AArch64::AEK_I8MM})},
    {"neoverse-v2", ARMV9A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_SVE, AArch64::AEK_SVE2, AArch64::AEK_SSBS,
          AArch64::AEK_FP16, AArch64::AEK_BF16, AArch64::AEK_RAND,
          AArch64::AEK_DOTPROD, AArch64::AEK_PROFILE, AArch64::AEK_SVE2BITPERM,
          AArch64::AEK_FP16FML, AArch64::AEK_I8MM, AArch64::AEK_MTE})},
    {"neoverse-v3", ARMV9_2A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_PROFILE, AArch64::AEK_MTE, AArch64::AEK_SSBS,
          AArch64::AEK_SB, AArch64::AEK_PREDRES, AArch64::AEK_LS64,
          AArch64::AEK_BRBE, AArch64::AEK_PAUTH, AArch64::AEK_FLAGM,
          AArch64::AEK_PERFMON, AArch64::AEK_RAND, AArch64::AEK_SVE2BITPERM,
          AArch64::AEK_FP16FML})},
    {"neoverse-v3ae", ARMV9_2A,
     (AArch64::ExtensionBitset(
         {AArch64::AEK_PROFILE, AArch64::AEK_MTE, AArch64::AEK_SSBS,
          AArch64::AEK_SB, AArch64::AEK_PREDRES, AArch64::AEK_LS64,
          AArch64::AEK_BRBE, AArch64::AEK_PAUTH, AArch64::AEK_FLAGM,
          AArch64::AEK_PERFMON, AArch64::AEK_RAND, AArch64::AEK_SVE2BITPERM,
          AArch64::AEK_FP16FML}))},
    {"cyclone", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_NONE})},
    {"apple-a7", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_NONE})},
    {"apple-a8", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_NONE})},
    {"apple-a9", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_NONE})},
    {"apple-a10", ARMV8A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_CRC, AArch64::AEK_RDM})},
    {"apple-a11", ARMV8_2A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_FP16})},
    {"apple-a12", ARMV8_3A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_FP16})},
    {"apple-a13", ARMV8_4A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_SHA3, AArch64::AEK_FP16,
                               AArch64::AEK_FP16FML})},
    {"apple-a14", ARMV8_5A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_SHA3, AArch64::AEK_FP16,
                               AArch64::AEK_FP16FML})},
    {"apple-a15", ARMV8_6A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_SHA3, AArch64::AEK_FP16,
                               AArch64::AEK_FP16FML})},
    {"apple-a16", ARMV8_6A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_SHA3, AArch64::AEK_FP16,
                               AArch64::AEK_FP16FML})},
    {"apple-a17", ARMV8_6A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_SHA3, AArch64::AEK_FP16,
                               AArch64::AEK_FP16FML})},

    {"apple-m1", ARMV8_5A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_SHA3, AArch64::AEK_FP16,
                               AArch64::AEK_FP16FML})},
    {"apple-m2", ARMV8_6A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_SHA3, AArch64::AEK_FP16,
                               AArch64::AEK_FP16FML})},
    {"apple-m3", ARMV8_6A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_SHA3, AArch64::AEK_FP16,
                               AArch64::AEK_FP16FML})},

    {"apple-s4", ARMV8_3A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_FP16})},
    {"apple-s5", ARMV8_3A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_FP16})},
    {"exynos-m3", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"exynos-m4", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_DOTPROD, AArch64::AEK_FP16})},
    {"exynos-m5", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_DOTPROD, AArch64::AEK_FP16})},
    {"falkor", ARMV8A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_CRC, AArch64::AEK_RDM})},
    {"saphira", ARMV8_3A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_PROFILE})},
    {"kryo", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"thunderx2t99", ARMV8_1A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2})},
    {"thunderx3t110", ARMV8_3A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2})},
    {"thunderx", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"thunderxt88", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"thunderxt81", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"thunderxt83", ARMV8A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_CRC})},
    {"tsv110", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_DOTPROD, AArch64::AEK_FP16,
                               AArch64::AEK_FP16FML, AArch64::AEK_PROFILE,
                               AArch64::AEK_JSCVT, AArch64::AEK_FCMA})},
    {"a64fx", ARMV8_2A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_FP16, AArch64::AEK_SVE})},
    {"carmel", ARMV8_2A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_AES, AArch64::AEK_SHA2, AArch64::AEK_FP16})},
    {"ampere1", ARMV8_6A,
     AArch64::ExtensionBitset({AArch64::AEK_AES, AArch64::AEK_SHA2,
                               AArch64::AEK_SHA3, AArch64::AEK_FP16,
                               AArch64::AEK_SB, AArch64::AEK_SSBS,
                               AArch64::AEK_RAND})},
    {"ampere1a", ARMV8_6A,
     AArch64::ExtensionBitset(
         {AArch64::AEK_FP16, AArch64::AEK_RAND, AArch64::AEK_SM4,
          AArch64::AEK_SHA3, AArch64::AEK_SHA2, AArch64::AEK_AES,
          AArch64::AEK_MTE, AArch64::AEK_SB, AArch64::AEK_SSBS})},
    {"ampere1b", ARMV8_7A,
     AArch64::ExtensionBitset({AArch64::AEK_FP16, AArch64::AEK_RAND,
                               AArch64::AEK_SM4, AArch64::AEK_SHA3,
                               AArch64::AEK_SHA2, AArch64::AEK_AES,
                               AArch64::AEK_MTE, AArch64::AEK_SB,
                               AArch64::AEK_SSBS, AArch64::AEK_CSSC})},
};

// Name alias.
struct Alias {
  StringRef AltName;
  StringRef Name;
};

inline constexpr Alias CpuAliases[] = {{"cobalt-100", "neoverse-n2"},
                                       {"grace", "neoverse-v2"}};

inline constexpr Alias ExtAliases[] = {{"rdma", "rdm"}};

bool getExtensionFeatures(
    const AArch64::ExtensionBitset &Extensions,
    std::vector<StringRef> &Features);

StringRef getArchExtFeature(StringRef ArchExt);
StringRef resolveCPUAlias(StringRef CPU);
StringRef resolveExtAlias(StringRef ArchExt);

// Information by Name
const ArchInfo *getArchForCpu(StringRef CPU);

// Parser
const ArchInfo *parseArch(StringRef Arch);
std::optional<ExtensionInfo> parseArchExtension(StringRef Extension);
// Given the name of a CPU or alias, return the correponding CpuInfo.
std::optional<CpuInfo> parseCpu(StringRef Name);
// Used by target parser tests
void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values);

bool isX18ReservedByDefault(const Triple &TT);

// For given feature names, return a bitmask corresponding to the entries of
// AArch64::CPUFeatures. The values in CPUFeatures are not bitmasks
// themselves, they are sequential (0, 1, 2, 3, ...).
uint64_t getCpuSupportsMask(ArrayRef<StringRef> FeatureStrs);

void PrintSupportedExtensions(StringMap<StringRef> DescMap);

} // namespace AArch64
} // namespace llvm

#endif
