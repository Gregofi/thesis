FROM debian

RUN apt-get update -y && apt upgrade -y && \
    apt-get install -y texlive

# Keep those two RUN separate to create new layer for texlive, which is large,
# every change in package then took forever.
RUN apt-get update && \
    apt-get install -y texlive-bibtex-extra biber \
    texlive-xetex \
    texlive-lang-czechslovak \
    latexmk \
    git \
    make
