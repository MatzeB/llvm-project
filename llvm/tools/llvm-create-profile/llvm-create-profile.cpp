// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This program creates an LLVM profile from an AutoFDO source.
// facebook T26943842

#include "llvm/Support/CommandLine.h"
#include <memory>

#include "LLVMProfileWriter.h"
#include "ProfileCreator.h"

llvm::cl::opt<std::string> Profile("profile",
                                   llvm::cl::desc("Input profile file name"),
                                   llvm::cl::Required);
llvm::cl::opt<std::string> Profiler("profiler",
                                    llvm::cl::desc("Input profile type"),
                                    llvm::cl::Required);
llvm::cl::opt<std::string>
    Out("out", llvm::cl::desc("Output profile file name"), llvm::cl::Required);
llvm::cl::list<std::string>
    Binary("binary", llvm::cl::desc("Binary file names"), llvm::cl::OneOrMore);
llvm::cl::opt<std::string>
    Format("format", llvm::cl::desc("Output file format"), llvm::cl::Required);

/// Debug options
llvm::cl::opt<std::string> PrintPerformanceSamples(
    "dump-perf-samples",
    llvm::cl::desc("print perf samples in the specified file"),
    llvm::cl::Optional);
llvm::cl::opt<std::string>
    PrintSymbolList("dump-symbols",
                    llvm::cl::desc("print symbol list  in the specified file"),
                    llvm::cl::Optional);

llvm::cl::opt<double>
    MinEventLineNumber("min-event-number",
                       llvm::cl::desc("skip the first <n> sampling events"),
                       llvm::cl::Optional);
llvm::cl::opt<double>
    MaxEventLineNumber("max-event-number",
                       llvm::cl::desc("stop after the <n>  events"),
                       llvm::cl::Optional);

#define PROG_USAGE                                                             \
  "\nConverts a sample profile collected with Perf "                           \
  "(https://perf.wiki.kernel.org/)\n"                                          \
  "into an LLVM profile. The output file can be used with\n"                   \
  "Clang's -fprofile-sample-use flag."

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv, PROG_USAGE);

  std::unique_ptr<autofdo::LLVMProfileWriter> writer(nullptr);
  if (Format.getValue() == "text") {
    writer.reset(new autofdo::LLVMProfileWriter(llvm::sampleprof::SPF_Text));
  } else if (Format.getValue() == "binary") {
    writer.reset(new autofdo::LLVMProfileWriter(llvm::sampleprof::SPF_Binary));
  } else if (Format.getValue() == "compbinary") {
    writer.reset(
        new autofdo::LLVMProfileWriter(llvm::sampleprof::SPF_Compact_Binary));
  } else {
    llvm::errs() << "--format must be one of 'text', 'binary' or 'compbinary'";
    return -1;
  }

  autofdo::ProfileCreator creator(
      std::vector<std::string>(Binary.begin(), Binary.end()));
  creator.set_use_discriminator_encoding(true);
  if (creator.CreateProfile(Profile, Profiler, writer.get(), Out))
    return 0;
  else
    return -1;
}
