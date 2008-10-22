
default: report.view

%.view: %.pdf
	$(READER) $*.pdf

%.pdf: %.tex
	pdflatex $*
	pdflatex $*

.PHONY: clean

clean:
	rm -f *.aux *.bbl *.blg *.log *.nav *.out *.snm *.toc *.vrb

