#!/usr/bin/env python3

import argparse
import collections
import os
import re
import shutil
import subprocess
import sys
import tempfile

# assumed upper bound on number of lines in a file
MAXLINES = 10000000
master="origin/toolchain/server/upstream"

def getArguments():
    parser = argparse.ArgumentParser(description=f"""
This progrom will identify differences between {master} and
some other revision, by default 'master', where those differences are
not marked with indications that the change is a faceboook change. The
associated source files are modified by adding the missing markers.
With the --revert option, this program looks for unmarked differences
between the current revision and the other revision and build a patch
file to eliminate those differences. This can be used to fix errors in
merging or where spaces were ignored when merging. When no sources are
specified, all files in the repository are considered.  An "unclean"
hunk is one that has both unexpected differences with master and
facebook changes -- will need manual update.
    """)

    parser.add_argument("--comment", help="associated task number", action="store")
    parser.add_argument("--rev", help="apply to all file changed since REV", action="store", default=master)
    parser.add_argument("--marker-span", help="number of lines between a begin marker and first change",
                        action="store", type=int, default=5)
    parser.add_argument("--revert", help="build revert patch", action="store_true")
    parser.add_argument("--exclude", help="regular expression describing source files to exclude", action="append")
    parser.add_argument("--include", help="regular expression describing source files to include", action="append")
    parser.add_argument("sources", help="files to be processed", nargs='*')
    parser.add_argument("--debug", help="enable debugging output", action="store_true")
    parser.add_argument("--verbose", help="enable verbose output", action="store_true")
    parser.add_argument("--add-markers", help="add missing markers", action="store_true")
    parser.add_argument("--build-patch", help="build a patch but don't apply it", action="store_true")
    return parser.parse_args()

def commandOutput(args):
    proc = subprocess.Popen(args,stdout=subprocess.PIPE,encoding='utf-8')
    return proc.stdout.readlines()

# match differential reference in commit message
diffRex = re.compile("    Differential Revision: https://phabricator.(fb|intern.facebook).com/(D[0-9]+)")

# memoized function to map a commit hash to a phabricator differential by
# scanning the commit message. Also returns
HashMap={}
def getDiff(Hash):
    if Hash in HashMap:
        return HashMap[Hash]
    if Hash == "000000000000":
        return "uncommitted"
    for line in commandOutput(['git','log', '-n', '1', Hash]):
        m = diffRex.match(line)
        if m:
            Diff = m.group(2)
            HashMap[Hash] = Diff
            return Diff
    for line in commandOutput(['git', 'branch', '--contains', Hash]):
        if (line.rstrip()[2:] == args.rev):
            return None
    return Hash   

# authorMailRex = re.compile("^author-mail <(.*)@(.*)>")
# hashRex = re.compile("^([0-9a-f]+) ([0-9]+) [0-9]+ [0-9]+")

hexRe="[0-9a-f]+"
dateRe="[0-9\-]+"
timeRe="[0-9:]+ [-+][0-9]+"
blankLineRe = re.compile("^\s*$")
# used to find leading what space
indentRe = re.compile("^(\s*)\S")

# pase an output line from git-blame-diff
diffBlameRe = re.compile("^ ([-+ ])\s+"
                         + '(' + hexRe + ')'
                         + " [^\\(]*\\((.*\S)\s+"
                         + dateRe + ' ' + timeRe +
                         "\s+([0-9]+)\\)\s[+\- ](.*)")

# map file extensions to default comment string
commentMap = { '.h' : "//",
               '.hpp' : "//",
               '.cc' : "//",
               '.cpp' : "//",
               '.php' : "//",
               '.cfg' : "#",  
               '.ll' : ';',
}

# determine the comment string for a file
def getComment(src):
    comment = args.comment
    if comment is not None:
        return comment
    _ , file_extension = os.path.splitext(src)
    if file_extension in commentMap:
        return commentMap[file_extension]
    if file_extension == ".txt":
        if os.path.basename(src) == "LLVMBuild.txt":
            return ";"
        return "#"
    if Verbose:
        print("unknown comment for extension", file_extension)
    return None

#
# Find a contiguous region of modified locations by facebook staff
# this block should begin with // facebook begin <diff or task reference>
# and end with // facebook end
# for one line blocks we have just // facebook <diff or task>> either
# before or on the line
#
# This function returns a list of where markers should be inserted
# to satisfy our team policy
#
# The repo is "dirty" in that there are some missed commits and hence
# there are spurious differences with master. These will show up
# as 'hunks' where there are no local diffs email addresses
# It seem we should first fix these....
#
def getMarkers(src):
    if Debug:
        print(src, file=sys.stdout)
    markers = collections.deque()
    comment= getComment(src)
    if comment is None:
        return markers
    CommentRe = re.compile("^(.*)" + comment + "\s*facebook(\s+(begin|end))?", re.IGNORECASE)

    Start = None
    StartLine = None
    UncleanHunk = False
    Diff = None
    LastBeginMarker = None
    Status = None # or Begin, or Single

    for line in commandOutput(['utils/fb-scripts/git-blame-diff', master, src]):
        if line[0] != ' ':
            continue
        m = diffBlameRe.match(line)
        if not m:
            sys.stderr.write(line)
            raise ValueError("could not parse git-blame-diff output")

        if m.group(1) == ' ':
            # before a change or inside a begin/end marker block
            if Start is None or Status == "begin":
                continue
            if Diff is not None:
                # add missing markers for facebook change
                if not StartMarker:
                    if  Start != Last:
                        markers.append((Start, "begin", "{}{} facebook begin {}".format(Indent,comment,Diff)))
                        markers.append((Last+1, "end", "{}{} facebook end {}".format(Indent,comment,Diff)))
                    else:
                        markers.append((Start, "single", "{}{} facebook {}".format(Indent,comment,Diff)))
                    LastBeginMarker = None
                elif Start == Last and re.search("facebook\s+begin", StartLine, re.IGNORECASE):
                    # a stray begin probably followed by some matching code
                    LastBeginMarker = Start
            Start = None
            Diff = None
            UncleanHunk = False;
            continue
        if m.group(1) == '-':
            continue
        Text = m.group(5)
        if blankLineRe.match(Text):
            continue
        Last = int(m.group(4))
        if Last == 1:
            # Change on first line implies added file
            return markers

        EndMarker = None
        MarkerError = None
        EndMarkerKind = ""
        LineAuthor = m.group(3)
        ThisHash = m.group(2)
        ThisDiff = getDiff(ThisHash)
        OldStatus = Status

        # match a facebook marker
        cm = CommentRe.match(Text)
        if cm:
            EndMarker = True
            EndMarkerKind = cm.group(3)
            if EndMarkerKind == "begin":
                if Status is not None:
                    MarkerError = "unexpected begin"
                Status = "begin"
            elif EndMarkerKind == "end":
                if Status != "begin":
                    MarkerError = "unexpected end"
                Status = None
            else: # single
                if Status is None:
                    if re.search("\S", cm.group(1)):
                        Status = None
                    else:
                        Status = "single"
        else:
            if Status is None:
                if Text.strip().rstrip() != "}":
                    MarkerError = "change outside marker"
                    Status = "New"
            elif Status == "single":
                Status = None
        if MarkerError is not None:
            sys.stderr.write(src + ": " + str(Last) + ":" + MarkerError + ": "+ Text + "\n")

        # No history is for this change
        if ThisDiff is None and Status != "begin" and not UncleanHunk:
            sys.stderr.write(src + ":" + str(Last) +": Unclean hunk\n")
            UncleanHunk = True
        elif Diff is None:
            Diff = ThisDiff
            Hash = ThisHash
            Author = LineAuthor

        if Start is None:
            if LastBeginMarker is not None and LastBeginMarker + args.marker_span >= Last:
                StartLine = LastBeginMarker
                StartMarker = 1
            else:
                Start = Last
                StartMarker = EndMarker
            StartLine = Text
            m2 = indentRe.match(Text)
            if m2:
                Indent = m2.group(1)
            else:
                Indent=""
            if Debug:
                print("start", Start, StartMarker, file=sys.stdout)
        if Debug and OldStatus != Status:
            print(OldStatus, ThisDiff, EndMarkerKind, m.group(3), m.group(4), m.group(5), file=sys.stdout)

    if Status is not None:
        sys.stderr.write(src+ ": misising end marker at end of file\n") 
            
    if Debug:
        for m in markers:
            print(m[0], m[1], m[2], file=sys.stdout)

    return markers

#
# rewrite src by inserting markers accounrding to the colelction markers
#
def applyMarkers(src, markers):
    fd, outname = tempfile.mkstemp()
    os.fchmod(fd, 0o644)
    with os.fdopen(fd, 'w') as output:
        with open(src) as input:
            # sentinel value 
            markers.append((MAXLINES, "final", ""))
            count = 1
            nextChange, style, text = markers.popleft()
            for line in input:
                if count == nextChange:
                    if Debug:
                        print('out: ', nextChange, style, text, file=sys.stdout)
                    # try to append to existing line
                    if style == "single":
                        m = text.strip()
                        l0 = line.rstrip()
                        if len(l0) + len(m) + 2 <= 80:
                            line = l0 + ' '+ m + '\n'
                        else:
                            output.write(text+'\n')
                    else:
                        output.write(text+'\n')
                    nextChange, style, text = markers.popleft()
                output.write(line)
                count = count + 1
    return outname

hunkRe = re.compile("^@@ -(\d+)(,(\d+))? \+(\d+)(,(\d+))? @@")
#
# parse a diff hunk header to get old/new half-open
# line intervals
# 
def parseHunkHeader(line):
    m = hunkRe.match(line)
    if not m:
        print("did not parse hunk header:", line.rstrip(), file=sys.stderr)
        raise ValueError("could not parse diff hunk header")

    old_start = int(m.group(1))
    old_count = int(m.group(3)) if m.group(2) else 1
    new_start = int(m.group(4))
    new_count = int(m.group(6)) if m.group(5) else 1
    return (old_start, old_start+old_count), (new_start, new_start+new_count)

#
# Determine where we seem to have some glitch where
# we have an unintended difference between the current branch
# which is a hunk where no changes involve Facebook diffs.
#
# Again, we start with the git-blame-diff output and map each modified
# line to "internal" or "external". Then we examine the "git diff" to
# look for hunks which are all "external"
#
def buildRevertPatch(src):
    AddChanges,RmChanges= getChanges(src)
    if not AddChanges:
        return None
    return buildPatch(AddChanges,RmChanges,src)

#
# Examine the git-blame-diff otuput to identify where an addition or
# removal is associated with Facebook ("Int") or not ("Ext").
#
def getChanges(src):
    AddChanges = collections.deque()
    RmChanges = collections.deque()
    for line in commandOutput(['git-blame-diff', master, src]):
        if line[0] != ' ' or line[1] == ' ':
            continue
        m = diffBlameRe.match(line)
        if not m:
            sys.stderr.write(line)
            raise ValueError("could not parse git-blame-diff output")
        ThisHash = m.group(2)
        ThisDiff = getDiff(ThisHash)
        IsExternal = "Ext" if ThisDiff is None else "Int"
        LineNo = int(m.group(4))
        if line[1] == '-':
            RmChanges.append((LineNo,IsExternal))
        else:
            AddChanges.append((LineNo, IsExternal))
        Author = m.group(3)
        if Debug:
            print(line[1], LineNo, IsExternal, Author, file=sys.stdout)

    return (AddChanges, RmChanges)


# This class holds information about a sequence of addition or removal
# and tests if a current hunk (assumed to follow any previous hunk)
# has internal or external changes which are separately reported by
# checkHunk
class HunkChecker:
    def __init__(self, d, m):
        self.m = m
        self.d = d
        self.d.append((MAXLINES, "End"))
        self.d.append((MAXLINES, "End"))
        if Debug:
            for i in self.d:
                print(m, i, file=sys.stdout)

        self.Cur, self.CurMode = self.d.popleft()
        self.Next, self.NextMode = self.d.popleft()
        
    def checkHunk(self, hunk):
        hunkStart, hunkEnd = hunk
        HasExt = False
        HasInt = False
        while (True) :
            if Debug:
                print("S,E,C,M,N =", hunkStart, hunkEnd, self.Cur, self.CurMode, self.Next, file=sys.stdout)

            # hunk contains Cur?
            if hunkStart <= self.Cur and self.Cur < hunkEnd:
                if self.CurMode == "Ext":
                    HasExt = True
                elif self.CurMode == "Int":
                    HasInt = True
                    
            if Debug:
                print("S,E,M,N,Ex,I =", hunkStart, hunkEnd, self.CurMode, self.Next, HasExt, HasInt, file=sys.stdout)
                        
            if hunkEnd < self.Next:
                break
            # advance to next change
            self.Cur, self.CurMode = self.Next, self.NextMode
            self.Next, self.NextMode = self.d.popleft()
        
        return HasExt, HasInt
    

#
# we build a new patch file by scanning an patch file and retaining
# only hunks which have only "external" changes.
#
def buildPatch(AddChanges, RmChanges, src):

    Added = HunkChecker(AddChanges,'+')
    Removed = HunkChecker(RmChanges,'-')
            
    fd, outname = tempfile.mkstemp()
    CopyLine = True
    NumHunks = 0
    with os.fdopen(fd, 'w') as output:
        for line in commandOutput(['git', 'diff', '-r', Revision, src]):

            if line[:2] == "@@":
                # a new chunk
                OldHunk, NewHunk = parseHunkHeader(line)
                if Debug:
                    print(line, file=sys.stdout)
                    print(OldHunk, NewHunk, file=sys.stdout)
                    
                HasExt, HasInt = Added.checkHunk(NewHunk)
                HasExtRm, HasIntRm = Added.checkHunk(NewHunk)
                
                CopyLine = False
                # copy of the chunk if it is external only
                if (HasExt or HasExtRm) and not (HasInt or HasIntRm):
                    CopyLine = True
                    NumHunks = NumHunks + 1
                    
            if CopyLine:
                output.write(line)
            
    if NumHunks == 0:
        os.remove(outname)
        return None
    
    return outname

#
# apply the --exclude and --include paramters to
# determine if this file should be processed
#
def shouldProcessFile(src):
    if not os.path.isfile(src):
        return False
    if args.exclude:
        for ex in args.exclude:
            if re.search(ex,src,re.IGNORECASE):
                return False
    if not args.include:
        return True
    for ix in args.include:
        if re.search(ix,src, re.IGNORECASE):
            return True
    return False

#
# Look where we may have allowed non-functional differences
# from master to creep in and build a diff to revert those changes
#
def matchMaster(src):
    patch = buildRevertPatch(src)
    if not patch:
        return
    if args.build_patch:
        patchfile = os.path.basename(src) + '.patch'
        if Verbose:
            print('path:', patchfile)
        shutil.move(patch, patchfile)
        return
    if Debug:
        with open(patch) as p:
            for line in p.readlines():
                sys.stdout.write(line)
    params = ['patch', '-R' ]
    if not Verbose:
        params.append('-s')
    params.extend([src,patch])
    subprocess.call(params)
    os.remove(patch)

args = getArguments()
Debug = args.debug
Verbose = Debug or args.verbose
Revision = args.rev

if not args.sources:
    sources = [ line.rstrip() for line in
                commandOutput(['git', 'diff', '-r', master, '--name-only']) ]
else:
    sources = args.sources

for src in sources:
    if not shouldProcessFile(src):
        if Verbose:
            print("Excluding", src)
        continue
    if Verbose:
        print("Processing", src)
    if args.revert:
        matchMaster(src)
        continue
    markers = getMarkers(src)
    if markers and args.add_markers:
        newfile = applyMarkers(src,markers)
        if newfile is not None:
            os.rename(newfile, src)


