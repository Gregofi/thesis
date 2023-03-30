.DEFAULT_GOAL := compile

clean:
	latexmk -C

compile: main.tex chapters/0.tex chapters/1.tex chapters/2.tex chapters/3.tex chapters/4.tex chapters/5.tex
	arara main
