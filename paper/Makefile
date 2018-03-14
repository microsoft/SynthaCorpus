all: pdf

# all the LaTeX sources

SOURCES=p

# what to call the final submission

SUBMISSION=indexingUsingLinkedLists.pdf

# special dependencies for each target
p.pdf: p.tex p.bib

term_dependence.pdf: term_dependence.tex

${SUBMISSION}.tex: p.tex p.bib

# ----------------------------------------------------------------------
# no user-servicable parts below

# lists of DVI, PS, PDF, and the other files LaTeX and bibtex leave
# around; generated from SOURCES above

TEX=$(SOURCES:%=%.tex)
BBL=$(SOURCES:%=%.bbl)
DVI=$(SOURCES:%=%.dvi)
PS=$(SOURCES:%=%.ps)
PDF=$(SOURCES:%=%.pdf)
EXTRAS=$(SOURCES:%=%.aux) $(SOURCES:%=%.bbl) $(SOURCES:%=%.blg) \
	$(SOURCES:%=%.log) $(SUBMISSION:%=%.aux) \
	$(SUBMISSION:%=%.log) $(SUBMISSION:%=%.tex) \
	$(SUBMISSION:%=%.bbl) $(SUBMISSION:%=%.blg) \
	$(SUBMISSION:%=%.pdf) $(SUBMISSION:%=%.ps) \
	$(SUBMISSION:%=%.dvi) \
	${DVI} ${PS} *~

# targets for PDF

pdf: ${PDF}

view: pdf
	gv ${PDF}

count:
	for f in ${SOURCES} ; do detex < $$f.tex | wc -w ; done

# rules for creating PDF

%.pdf: %.tex
	pdflatex $*
	-BIBINPUTS=. bibtex $*
	pdflatex $*
	latex_count=5 ; \
	while egrep -s -m 1 'Rerun (LaTeX|to get cross-references right|to get citations)' $*.log && [ $$latex_count -gt 0 ] ; do \
	      pdflatex $< ; latex_count=`expr $$latex_count - 1` ; \
	done
#	-rm -f ${EXTRAS}

# rules for creating DVI

%.dvi: %.tex
	latex $*
	bibtex $*
	latex $*
	latex_count=5 ; \
	while egrep -s -m 1 'Rerun (LaTeX|to get cross-references right)' $*.log && [ $$latex_count -gt 0 ] ; do \
	      latex $< ; latex_count=`expr $$latex_count - 1` ; \
	done

# rules for turning PDF into PS

ps: ${PS}

%.ps: %.pdf
	acroread -toPostScript $<

# generating the final version -- the same but with .bbl inserted,
# instead of running bibtex

submission: ${SUBMISSION}.pdf ${SUBMISSION}.ps

# expand all the \includes and the .bbl file, to get an all-in-one LaTeX
# file; also a hack for "fixing" spacing around our small bibitems
${SUBMISSION}.tex:
	./latexpand --expand-bbl ${BBL} ${TEX} | \
	perl -npe 'BEGIN{$$seen=0} if (!$$seen && /^\\bibitem/){$$seen=1;print "\\vspace*{\\baselineskip}\n"}' > $@

# this is a horrid hack, which embeds all the fonts by passing through
# gs.  There are two problems: (a) pdflatex needs to embed its fonts
# (it does this by default these days) and (b) every included pdf
# graphic needs to embed fonts, too.  So either we have to make sure
# *every* R plot embeds fonts, or we do this.
${SUBMISSION}.pdf: ${SUBMISSION}.tex
	pdflatex ${SUBMISSION}
	latex_count=5 ; \
	while egrep -s -m 1 'Rerun (LaTeX|to get cross-references right)' ${SUBMISSION}.log && [ $$latex_count -gt 0 ] ; do \
	      pdflatex ${SUBMISSION} ; latex_count=`expr $$latex_count - 1` ; \
	done
	gs -dNOPAUSE -dBATCH -sDEVICE=pdfwrite -dPDFSETTINGS=/printer -dEmbedAllFonts=true -dNOPLATFONTS -sOutputFile=/tmp/${SUBMISSION}.pdf -f ${SUBMISSION}.pdf
	mv /tmp/${SUBMISSION}.pdf .

# other targets

clean:
	-rm -f ${EXTRAS}
