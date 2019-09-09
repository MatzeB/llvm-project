// facebook T53546053
//===- CFGChangeLogHandler.h -----------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header defines a class that provides an interface for CFG change
/// logging. The interface is required for LegacyPassManager.
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CFGCHANGELOGHANDLER_H
#define LLVM_IR_CFGCHANGELOGHANDLER_H

#include "llvm/ADT/StringRef.h"
#include <functional>

namespace llvm {

template <class FuncT> class CFGChangeLogHandler {
public:
  // Functions to register callbacks.
  void registerHasLoggingTarget(std::function<bool()> Callback) {
    hasLoggingTarget = Callback;
  }

  void registerIsLoggingTarget(std::function<bool(StringRef)> Callback) {
    isLoggingTarget = Callback;
  }

  void registerRunAfter(
      std::function<void(const FuncT *, const std::string &)> Callback) {
    runAfter = Callback;
  }

  // Functions to call registerd callbacks.
  bool callHasLoggingTarget() {
    if (!hasLoggingTarget)
      return false;
    return hasLoggingTarget();
  }

  bool callIsLoggingTarget(StringRef Name) {
    if (!callHasLoggingTarget())
      return false;
    assert(isLoggingTarget);
    return isLoggingTarget(Name);
  }

  void callRunAfter(const FuncT *F, const std::string &Banner) {
    assert(runAfter);
    runAfter(F, Banner);
  }

private:
  // Check if there's any function to log its CFG changes.
  std::function<bool()> hasLoggingTarget;
  // Check if the function with the given name is CFG change logging target.
  std::function<bool(StringRef)> isLoggingTarget;
  // A function to run after the transformation for CFG change logging.
  std::function<void(const FuncT *, const std::string &)> runAfter;
};

template <class FuncT> CFGChangeLogHandler<FuncT> &getCFGChangeLogHandler() {
  static CFGChangeLogHandler<FuncT> Handler;
  return Handler;
}

} // namespace llvm

#endif
