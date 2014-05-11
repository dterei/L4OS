default: report.pdf

%.view: %.pdf
	$(READER) $*.pdf

%.pdf: %.tex
	pdflatex $*
	pdflatex $*

.PHONY: clean
clean:
	rm -f report.pdf
	rm -f *.aux *.bbl *.blg *.log *.nav *.out *.snm *.toc *.vrb

