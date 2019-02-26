This readme is people wanting to understand the source code and possiblity contribute. It explains the source code
layout and the algorithm used to generate the profile.

# Difference with the orginal tools

 1- Symbols are loaded from the debug info instead of using the symbol tables. This helps for better supports
 of stripped binaries
 2- Support for dynamic libraries 
 3 - Since this tools is using a diffirent  library, the output and symbol names sometime differs .   


# Introduction

The profile generation is structured in tree phases : loading the profile samples implemented in the AbstractReader class
hierarchy , loading the symbols done in SymbolMap::BuildSymboMap and finally the aggregation of all the symbols in symbol_map.cpp and profile.cpp



## Loading samples 

The sample_readers  convert addresses into symbolizable addresses represented by `InstructionLocation` objects. The offset
in `InstructionLocation` objects are offset compatible with what `llvm::symbolizer` expects. They represent the prefered 
virtul addresses, ei the addresses an instruction would have if the system loader were to respect the information in the segments headers.
In practice, the preffered virtual address and the runtime virtual address are the same for executable but not for dynamic object.



The samples are organized in three groups : instructions addresses, ranges and branches; Each group stored in a corresponding
map where the key is the sample type , and the value the count associated to the key eg rangeMap[range1] = a implies,
range1 has been recorded a time in the profile. 

The instructions addresses and the ranges are used to compute the execution count of instructions , while the branches are used
mainly for call target resolution. When computing the instructions execution counts, we exclusivelly use either the address
samples or the ranges samples and never both at the same time. Which type of sample is used is controled by the useLBR command
line option. When the ranges are use, we first expand them into a series of address samples covering the instructions inside the range.
associating with each instructions sample the count of the range. ie a range {0x1,0x3}:6 becomes  [{0x1}:6, {0x2}:6,{0x3}:6]; 
(see Profile::ComputeProfile)





### Text file format 
The text file format only support a single use with a single 


### perf file format  
0

## Loading symbols 
The symbols are loaded directly from the debug sections. We have two representation of symbols. During the loading and parsing
of the symbols, each symbol is extracted as a { InstructionLocation  functionStart , std::string functionName, size_t function size }


## Computing and aggregating symbols 

The final profile is represented by a forest. The trees are represened by instance of the `Symbol`. The root of the trees represent 
real function from the binary, other three nodes represent inlined subroutine. 
Each samples is symbolicated individually and the result of the symbolication is merge into the profile. 
 


//TODO : give example on how the events are broken down. 


 

