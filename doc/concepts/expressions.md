Expression language
===================

At various places, in particular in order to define a rule, we need a
restricted form of functional computation. This is achieved by our
expression language.

Syntax
------

All expressions are given by JSON values. One can think of expressions
as abstract syntax trees serialized to JSON; nevertheless, the precise
semantics is given by the evaluation mechanism described later.

Semantic Values
---------------

Expressions evaluate to semantic values. Semantic values are JSON values
extended by additional atomic values for build-internal values like
artifacts, names, etc.

### Truth

Every value can be treated as a boolean condition. We follow a
convention similar to `LISP` considering everything true that is not
empty. More precisely, the values

 - `null`,
 - `false`,
 - `0`,
 - `""`,
 - the empty map, and
 - the empty list

are considered logically false. All other values are logically true.

Evaluation
----------

The evaluation follows a strict, functional, call-by-value evaluation
mechanism; the precise evaluation is as follows.

 - Atomic values (`null`, booleans, strings, numbers) evaluate to
   themselves.
 - For lists, each entry is evaluated in the order they occur in the
   list; the result of the evaluation is the list of the results.
 - For JSON objects (which can be understood as maps, or dicts), the key
   `"type"` has to be present and has to be a literal string. That
   string determines the syntactical construct (sloppily also referred
   to as "function") the object represents, and the remaining
   evaluation depends on the syntactical construct. The syntactical
   construct has to be either one of the built-in ones or a special
   function available in the given context (e.g., `"ACTION"` within the
   expression defining a rule).

All evaluation happens in an "environment" which is a map from strings
to semantic values.

### Built-in syntactical constructs

#### Special forms

##### Variables: `"var"`

There has to be a key `"name"` that (i.e., the expression in the
object at that key) has to be a literal string, taken as
variable name. If the variable name is in the domain of the
environment and the value of the environment at the variable
name is non-`null`, then the result of the evaluation is the
value of the variable in the environment.

Otherwise, the key `"default"` is taken (if present, otherwise
the value `null` is taken as default for `"default"`) and
evaluated. The value obtained this way is the result of the
evaluation.

##### Quoting: `"'"`

The value is the value of the key `"$1"` uninterpreted, if present,
and `null` otherwise.

##### Quasi-Quoting: ``"`"``

The value is the value of the key `"$1"` uninterpreted but replacing
all outermost maps having a key `"type"` with the value either
`","` or `",@"` in the following way.
 - If the value for the key `"type"` is `","`, the value for the
   key `"$1"` (or `null` if there is no key `"$1"`) is evaluated
   and the map is replaced by the value of that evaluation.
 - If the value for the key `"type"` is `",@"` it is an error if
   that map is not an entry of a literal list. The value for the
   key `"$1"` (or `[]` if there is no key `"$1"`) is evaluated;
   the result of that evaluation has to be a list. The entries of
   that list (i.e., the list obtained by evaluating the value for
   `"$1"`) are inserted (not the list itself) into the surrounding
   list replacing that map.
For example, ``{"type": "`", "$1": [1, 2, {"type": ",@", "$1": [3, 4]}]}``
evaluates to `[1, 2, 3, 4]` while
``{"type": "`", "$1": [1, 2, {"type": ",", "$1": [3, 4]}]}``
evaluates to `[1, 2, [3, 4]]`.


##### Sequential binding: `"let*"`

The key `"bindings"` (default `[]`) has to be (syntactically) a
list of pairs (i.e., lists of length two) with the first
component a literal string.

For each pair in `"bindings"` the second component is evaluated,
in the order the pairs occur. After each evaluation, a new
environment is taken for the subsequent evaluations; the new
environment is like the old one but amended at the position
given by the first component of the pair to now map to the value
just obtained.

Finally, the `"body"` is evaluated in the final environment
(after evaluating all binding entries) and the result of
evaluating the `"body"` is the value for the whole `"let*"`
expression.

##### Environment Map: `"env"`

Creates a map from selected environment variables.

The key `"vars"` (default `[]`) has to be a list of literal
strings referring to the variable names that should be included
in the produced map. This field is not evaluated. This
expression is only for convenience and does not give new
expression power. It is equivalent but lot shorter to multiple
`singleton_map` expressions combined with `map_union`.

##### Conditionals

###### Binary conditional: `"if"`

First the key `"cond"` is evaluated. If it evaluates to a value that
is logically true, then the key `"then"` (if present, otherwise `[]`
is taken as default) is evaluated and its value is the result of
the evaluation. Otherwise, the key `"else"` (if present, otherwise
`[]` is taken as default) is evaluated and the obtained value is
the result of the evaluation.

###### Sequential conditional: `"cond"`

The key `"cond"` has to be a list of pairs. In the order of
the list, the first components of the pairs are evaluated,
until one evaluates to a value that is logically true. For
that pair, the second component is evaluated and the result
of this evaluation is the result of the `"cond"` expression.

If all first components evaluate to a value that is
logically false, the result of the expression is the result
of evaluating the key `"default"` (defaulting to `[]`).

###### String case distinction: `"case"`

If the key `"case"` is present, it has to be a map (an
"object", in JSON's terminology). In this case, the key
`"expr"` is evaluated; it has to evaluate to a string. If
the value is a key in the `"case"` map, the expression at
this key is evaluated and the result of that evaluation is
the value for the `"case"` expression.

Otherwise (i.e., if `"case"` is absent or `"expr"` evaluates
to a string that is not a key in `"case"`), the key
`"default"` (with default `[]`) is evaluated and this gives
the result of the `"case"` expression.

###### Sequential case distinction on arbitrary values: `"case*"`

If the key `"case"` is present, it has to be a list of
pairs. In this case, the key `"expr"` is evaluated. It is an
error if that evaluates to a name-containing value. The
result of that evaluation is sequentially compared to the
evaluation of the first components of the `"case"` list
until an equal value is found. In this case, the evaluation
of the second component of the pair is the value of the
`"case*"` expression.

If the `"case"` key is absent, or no equality is found, the
result of the `"case*"` expression is the result of
evaluating the `"default"` key (with default `[]`).

##### Conjunction and disjunction: `"and"` and `"or"`

For conjunction, if the key `"$1"` (with default `[]`) is
syntactically a list, its entries are sequentially evaluated
until a logically false value is found; in that case, the result
is `false`, otherwise true. If the key `"$1"` has a different
shape, it is evaluated and has to evaluate to a list. The result
is the conjunction of the logical values of the entries. In
particular, `{"type": "and"}` evaluates to `true`.

For disjunction, the evaluation mechanism is the same, but the
truth values and connective are taken dually. So, `"and"` and
`"or"` are logical conjunction and disjunction, respectively,
using short-cut evaluation if syntactically admissible (i.e., if
the argument is syntactically a list).

##### Mapping

###### Mapping over lists: `"foreach"`

First the key `"range"` is evaluated and has to evaluate to
a list. For each entry of this list, the expression `"body"`
is evaluated in an environment that is obtained from the
original one by setting the value for the variable specified
at the key `"var"` (which has to be a literal string,
default `"_"`) to that value. The result is the list of
those evaluation results.

###### Mapping over maps: `"foreach_map"`

Here, `"range"` has to evaluate to a map. For each entry (in
lexicographic order (according to native byte order) by
keys), the expression `"body"` is evaluated in an
environment obtained from the original one by setting the
variables specified at `"var_key"` and `"var_val"` (literal
strings, default values `"_"` and `"$_"`, respectively). The
result of the evaluation is the list of those values.

##### Folding: `"foldl"`

The key `"range"` is evaluated and has to evaluate to a list.
Starting from the result of evaluating `"start"` (default `[]`)
a new value is obtained for each entry of the range list by
evaluating `"body"` in an environment obtained from the original
by binding the variable specified by `"var"` (literal string,
default `"_"`) to the list entry and the variable specified by
`"accum_var"` (literal string, default value `"$1"`) to the old
value. The result is the last value obtained.

#### Regular functions

First `"$1"` is evaluated; for binary functions `"$2"` is evaluated
next. For functions that accept keyword arguments, those are
evaluated as well. Finally the function is applied to this (or
those) argument(s) to obtain the final result.

##### Unary functions

 - `"not"` Return the logical negation of the argument, i.e.,
   if the argument is logically false, return `true`, and `false`
   otherwise.

 - `"nub_right"` The argument has to be a list. It is an error
   if that list contains (directly or indirectly) a name. The
   result is the input list, except that for all duplicate
   values, all but the rightmost occurrence is removed.

 - `"basename"` The argument has to be a string. This string is
   interpreted as a path, and the file name thereof is
   returned.

 - `"keys"` The argument has to be a map. The result is the
   list of keys of this map, in lexicographical order
   (according to native byte order).

 - `"values"` The argument has to be a map. The result are the
   values of that map, ordered by the corresponding keys
   (lexicographically according to native byte order).

 - `"range"` The argument is interpreted as a non-negative
   integer as follows. Non-negative numbers are rounded to the
   nearest integer; strings have to be the decimal
   representation of an integer; everything else is considered
   zero. The result is a list of the given length, consisting
   of the decimal representations of the first non-negative
   integers. For example, `{"type": "range",
    "$1": "3"}` evaluates to `["0", "1", "2"]`.

 - `"enumerate"` The argument has to be a list. The result is a
   map containing one entry for each element of the list. The
   key is the decimal representation of the position in the
   list (starting from `0`), padded with leading zeros to
   length at least 10. The value is the element. The padding is
   chosen in such a way that iterating over the resulting map
   (which happens in lexicographic order of the keys) has the
   same iteration order as the list for all lists indexable by
   32-bit integers.

- `"set"` The argument has to be a list of strings. The result is
  a map with the members of the list as keys, and all values being
  `true`.

- `"reverse"` The argument has to be a list. The result is a new list
  with the entries in reverse order.

- `"length"` The argument has to be a list. The result is the length
  of the list.

 - `"++"` The argument has to be a list of lists. The result is
   the concatenation of those lists.

 - `"+"` The argument has to be a list of numbers. The result is
   their sum (where the sum of the empty list is, of course, the
   neutral element 0).

 - `"*"` The argument has to be a list of numbers. The result
   is their product (where the product of the empty list is, of
   course, the neutral element 1).

 - `"map_union"` The argument has to be a list of maps. The
   result is a map containing as keys the union of the keys of
   the maps in that list. For each key, the value is the value
   of that key in the last map in the list that contains that
   key.

 - `"join_cmd"` The argument has to be a list of strings. A
   single string is returned that quotes the original vector in
   a way understandable by a POSIX shell. As the command for an
   action is directly given by an argument vector, `"join_cmd"`
   is typically only used for generated scripts.

 - `"json_encode"` The result is a single string that is the
   canonical JSON encoding of the argument (with minimal white
   space); all atomic values that are not part of JSON (i.e.,
   the added atomic values to represent build-internal values)
   are serialized as `null`.

##### Unary functions with keyword arguments

 - `"change_ending"` The argument has to be a string,
   interpreted as path. The ending is replaced by the value of
   the keyword argument `"ending"` (a string, default `""`).
   For example, `{"type":
    "change_ending", "$1": "foo/bar.c", "ending": ".o"}`
   evaluates to `"foo/bar.o"`.

 - `"join"` The argument has to be a list of strings. The
   return value is the concatenation of those strings,
   separated by the the specified `"separator"` (strings,
   default `""`).

 - `"escape_chars"` Prefix every in the argument every
   character occurring in `"chars"` (a string, default `""`) by
   `"escape_prefix"` (a strings, default `"\"`).

 - `"to_subdir"` The argument has to be a map (not necessarily
   of artifacts). The keys as well as the `"subdir"` (string,
   default `"."`) argument are interpreted as paths and keys
   are replaced by the path concatenation of those two paths.
   If the optional argument `"flat"` (default `false`)
   evaluates to a true value, the keys are instead replaced by
   the path concatenation of the `"subdir"` argument and the
   base name of the old key. It is an error if conflicts occur
   in this way; in case of such a user error, the argument
   `"msg"` is also evaluated and the result of that evaluation
   reported in the error message. Note that conflicts can also
   occur in non-flat staging if two keys are different as
   strings, but name the same path (like `"foo.txt"` and
   `"./foo.txt"`), and are assigned different values. It also
   is an error if the values for keys in conflicting positions
   are name-containing.

 - `"from_subdir"` The argument has to be a map (not necessarily of
   artifacts). The keys of this map, as well as the value of keyword
   argument `"subdir"` (string, default `"."`) are interpreted as
   paths; only those key-value pairs of the argument map are kept
   where the key refers to an entry in the specified `"subdir"`,
   and for those the path relative to the subdir is taken as new
   key. Those paths relative to the subdir are taken in canonical
   form; it is an error if non-trivial conflicts arise that way,
   i.e., if two keys that are kept normalize to the same relative
   path while the repsective values are different.

##### Binary functions

 - `"=="` The result is `true` is the arguments are equal,
   `false` otherwise. It is an error if one of the arguments
   are name-containing values.

 - `"concat_target_name"` This function is only present to
   simplify transitions from some other build systems and
   normally not used outside code generated by transition
   tools. The second argument has to be a string or a list of
   strings (in the latter case, it is treated as strings by
   concatenating the entries). If the first argument is a
   string, the result is the concatenation of those two
   strings. If the first argument is a list of strings, the
   result is that list with the second argument concatenated to
   the last entry of that list (if any).

##### Other functions

 - `"empty_map"` This function takes no arguments and always
   returns an empty map.

 - `"singleton_map"` This function takes two keyword arguments,
   `"key"` and `"value"` and returns a map with one entry,
   mapping the given key to the given value.

 - `"lookup"` This function takes two keyword arguments,
   `"key"` and `"map"`. The `"key"` argument has to evaluate to
   a string and the `"map"` argument has to evaluate to a map.
   If that map contains the given key and the corresponding
   value is non-`null`, the value is returned. Otherwise the
   `"default"` argument (with default `null`) is evaluated and
   returned.

 - `"[]"` This function takes two keyword arguments, `"index"` and
   `"list"`. The `"list"` argument has to evaluate to a list. The
   `"index"` argument has to evaluate to either a number (which
   is then rounded to the nearest integer) or a string (which
   is interpreted as an integer). If the index obtained in this
   way is valid for the obtained list, the entry at that index
   is returned; negative indices count from the end of the list.
   Otherwise the `"default"` argument (with default `null`) is
   evaluated and returned.

#### Constructs related to reporting of user errors

Normally, if an error occurs during the evaluation the error is
reported together with a stack trace. This, however, might not be
the most informative way to present a problem to the user,
especially if the underlying problem is a proper user error, e.g.,
in rule usage (leaving out mandatory arguments, violating semantical
prerequisites, etc). To allow proper error reporting, the following
functions are available. All of them have an optional argument
`"msg"` that is evaluated (only) in case of error and the result of
that evaluation included in the error message presented to the user.

 - `"fail"` Evaluation of this function unconditionally fails.

 - `"context"` This function is only there to provide additional
   information in case of error. Otherwise it is the identify
   function (a unary function, i.e., the result of the evaluation
   is the result of evaluating the argument `"$1"`).

 - `"assert_non_empty"` Evaluate the argument (given by the
   parameter `"$1"`). If it evaluates to a non-empty string, map,
   or list, return the result of the evaluation. Otherwise fail.

 - `"disjoint_map_union"` Like `"map_union"` but it is an error, if
   two (or more) maps contain the same key, but map it to different
   values. It is also an error if the argument is a name-containing
   value.

 - `"assert"` Evaluate the argument (given by the parameter `"$1"`);
   then evaluate the expression `"predicate"` with the variable given
   at the key `"var"` (which has to be a string literal if given,
   default value is `"_"`) bound to that value. If the predicate
   evaluates to a true value, return the result of evaluating the
   argument, otherwise fail; in evaluating the failure message
   `"msg"`, also keep the variable specified by `"var"` bound to
   the result of evaluating the argument.
