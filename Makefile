.DEFAULT_GOAL := compile

clean:
	rm -rd out

compile:
	mkdir -p out/text
	latexmk -pdflatex=lualatex -bibtex -output-directory=out -shell-escape -pdf -file-line-error
