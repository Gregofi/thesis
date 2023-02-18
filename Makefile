.DEFAULT_GOAL := compile

clean:
	rm -rd out

compile: main.tex chapters/0.tex chapters/1.tex
	mkdir -p out/text
	latexmk -xelatex -bibtex -output-directory=out -shell-escape -pdf
