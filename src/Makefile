CC=/usr/bin/gcc
CFLAGS=-O3 -std=c11 -m64 -Wall -Wno-pointer-sign
LDLIBS=-lm
# For an explanation of automatic variables (e.g.$@, $? and $^) see
# https://www.gnu.org/software/make/manual/make.html#Flavors
#

all: corpusPropertyExtractor.exe dahashDemo.exe queryGenerator.exe corpusGenerator.exe convertPGtoSTARC.exe selectRecordsFromFile.exe checkSTARCFile.exe countDocsInCorpus.exe

queryGenerator.exe:	queryGenerator.o qGeneratorArgTable.o characterSetHandling/unicode.o imported/Fowler-Noll-Vo-hash/fnv.o imported/TinyMT_cutdown/tinymt64.o utils/argParser.o utils/dahash.o utils/dynamicArrays.o utils/general.o utils/randomNumbers.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

selectRecordsFromFile.exe:	selectRecordsFromFile.o utils/general.o imported/TinyMT_cutdown/tinymt64.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

checkSTARCFile.exe:	checkSTARCFile.o utils/general.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

countDocsInCorpus.exe:	countDocsInCorpus.o utils/general.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)


convertPGtoSTARC.exe:	convertPGtoSTARC.o  utils/general.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

corpusPropertyExtractor.exe: corpusPropertyExtractor.o extractorArgTable.o generateTFDFiles.o ngramFunctions.o characterSetHandling/unicode.o imported/Fowler-Noll-Vo-hash/fnv.o imported/TinyMT_cutdown/tinymt64.o utils/argParser.o utils/dahash.o utils/dynamicArrays.o utils/general.o utils/randomNumbers.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

dahashDemo.exe:	utils/dahashDemo.o utils/dahash.o imported/Fowler-Noll-Vo-hash/fnv.o utils/general.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

corpusGenerator.exe:	corpusGenerator.o syntheticCollections.o simpleWords.o bubbleBabble.o MarkovWords.o termRepMethods.o shuffling.o wordFeatures.o subsumptionLists.o corpusGeneratorArgTable.o utils/argParser.o utils/dahash.o utils/dynamicArrays.o utils/general.o utils/randomNumbers.o imported/TinyMT_cutdown/tinymt64.o characterSetHandling/unicode.o imported/Fowler-Noll-Vo-hash/fnv.o

	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

doc: ../doc/how_to_synthesize_a_corpus.tex ../doc/user_overview.tex ../doc/developer_overview.tex
	pdflatex ../doc/how_to_synthesize_a_corpus
	pdflatex ../doc/user_overview
	pdflatex ../doc/developer_overview

clean:
	/bin/rm -f *.a *.exe *.dll


cleaner: clean
	/bin/rm -f *.o  *~
	/bin/rm -f utils/*.o characterSetHandling/*.o
	/bin/rm -f imported/TinyMT_cutdown/tinymt64.o
	/bin/rm -f imported/Fowler-Noll-Vo-hash/fnv.o

cleanest: cleaner

git:
	git add `find . -name \*.pl`
	git add `find . -name \*.c`
	git add `find . -name \*.h`
	git add `find . -name \*.sln`
	git add `find . -name \*.vcxproj`
	git add `find .. -iname \*README\*`
	git add `find .. -iname \*LICENSE\*`
	git add `find .. -iname \*VERSION\*`
	git add `find .. -iname \*CONTRIBUTING\*`
	git add `find .. -name Makefile\*`
	git add `find ../doc -iname \*.tex`
	git add `find ../doc -iname \*.pdf`

	echo "\nPlease run git commit -m \"<commit message>\" and git push"


