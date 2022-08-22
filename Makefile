.DEFAULT_GOAL := compile

clean:
	rm out -rd

compile:
	mkdir -rp out/text
	latexmk -xelatex -bibtex -output-directory=out