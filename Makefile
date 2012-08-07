RST2HTML:=rst2html
SOURCES=index.rst 
CSSFILE:=images/default.css

all: index.html
	
index.html: $(SOURCES) $(CSSFILE)
	$(RST2HTML) --stylesheet=$(CSSFILE) --link-stylesheet index.rst > index.html

clean:
	rm -f index.html *~

