TOP_SRCDIR = $(abs_top_srcdir)/src

vpath %.lyx $(MRP_DOCDIR)/plugin-developer-guide/lyx
vpath %.xml $(MRP_DOCDIR)/plugin-developer-guide/doxml

include $(MRP_MAKE_DOCRULES)


FIGURES_SVG = db-layers.svg
FIGURES_PNG = $(FIGURES_SVG:.svg=.png)
FIGURES_PDF = $(FIGURES_SVG:.svg=.pdf)

FIGURES = $(FIGURES_SVG) $(FIGURES_PNG) $(FIGURES_PDF)


high_level_api_definition_SRC = mql.h
high_level_api_definition_DOXML = $(high_level_api_definition_SRC:.h=_8h.xml)


TARGETS = $(FIGURES) plugin-developer-guide.xml


xmldir = $(MRP_DOCDIR)
nodist_xml_DATA = $(TARGETS)


all-am: $(TARGETS)


mql-grammar.xml: $(TOP_SRCDIR)/murphy-db/mql/mql-scanner.l \
                 $(TOP_SRCDIR)/murphy-db/mql/mql-parser.y
	$(MRP_ABNF) $+ > $@

high-level-api-definition.xml: $(high_level_api_definition_DOXML)
	$(MRP_DOXML2DB) ../doxml $^ $@

murphy-db-introduction.xml: murphy-db-introduction.lyx

murphy-db-high-level-api.xml: murphy-db-high-level-api.lyx \
                              high-level-api-definition.xml

murphy-db-query-language.xml: murphy-db-query-language.lyx \
                              mql-grammar.xml


plugin-developer-guide.xml: plugin-developer-guide.lyx \
                            murphy-db-introduction.xml \
                            murphy-db-high-level-api.xml \
                            murphy-db-query-language.xml

clean-local:
	rm -f *~ $(TARGETS) \
                murphy-db-introduction.xml \
                murphy-db-high-level-api.xml high-level-api-definition.xml \
                murphy-db-query-language.xml mql-grammar.xml \
		../lyx/*.xml ../lyx/*~
