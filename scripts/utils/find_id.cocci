virtual patch
virtual report

@initialize:python@
@@

@find_id@
identifier id;
type ty;
parameter list[nb_params] params;
position p;
@@
(
  ty id@p(params);
|
  extern ty id@p;
)

@script:python@
id << find_id.id;
p << find_id.p;
@@
print("identifier %s %s %s" % (id, p[0].file, p[0].line))
