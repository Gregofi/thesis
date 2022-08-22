.DEFAULT_GOAL := compile

clean:
	rm out -rd

compile:
	mkdir -p out/text
	latexmk -xelatex -bibtex -output-directory=out