#include <u.h>
#include <libc.h>

/* changes to original code: 
   1. sysfatal() is used for the FAIL() function 
   2. read() is turned to read_() to avoid name conflicts with Plan 9 read()
   2. state nil is turned to null (temporary workaround, need rewriting code to accomodate nil in u.h ((void*)0)
*/

/*******************************************************************
 * Params
 ******************************************************************/

#define DEBUG 0
#define USE_LOWLEVEL 1

/*******************************************************************
 * Object
 ******************************************************************/

#define TAG_NIL  0
#define TAG_ATOM 1
#define TAG_NUM  2
#define TAG_PAIR 3
#define TAG_CLOS 4
#define TAG_PRIM 5

#define IS_NIL(obj)  ((obj)->tag == TAG_NIL)
#define IS_ATOM(obj) ((obj)->tag == TAG_ATOM)
#define IS_NUM(obj)  ((obj)->tag == TAG_NUM)
#define IS_PAIR(obj) ((obj)->tag == TAG_PAIR)
#define IS_CLOS(obj) ((obj)->tag == TAG_CLOS)
#define IS_PRIM(obj) ((obj)->tag == TAG_PRIM)

// Common object structure
typedef struct obj obj_t;
struct obj
{
  ulong tag;
  union {
    const char * atom;
    uvlong       num;
    struct {
      obj_t * car;
      obj_t * cdr;
    } pair;
    struct {
      obj_t *body;
      obj_t *env;
    } clos;
    struct {
      void (*func)(obj_t **env);
    } prim;
  };
};

#define FLAG_PUSH 128
#define FLAG_POP  129

// Global State
typedef struct state state_t;
struct state
{
  char  * input_str;             // input data string used by read_()
  uvlong  input_len;             // input data length used by read_()
  uvlong  input_pos;             // input data position used by read_()

  obj_t * null;                  // null: ()
  obj_t * read_stack;            // defered obj to emit from read

  obj_t * interned_atoms;        // interned atoms list
  obj_t * atom_true;             // atom: t
  obj_t * atom_quote;            // atom: quote
  obj_t * atom_push;             // atom: push
  obj_t * atom_pop;              // atom: pop

  obj_t * stack;                 // top-of-stack (implemented with pairs)
  obj_t * env;                   // top-level / initial environment
};
state_t state[1];

obj_t *alloc(void)
{
  return (obj_t*)malloc(sizeof(obj_t));
}

obj_t *make_null(void)
{
  obj_t *obj = alloc();
  obj->tag = TAG_NIL;
  return obj;
}

obj_t *make_atom(const char *str, uvlong len)
{
  char *atom_str = malloc(len+1);
  memcpy(atom_str, str, len);
  atom_str[len] = '\0';

  obj_t *atom = alloc();
  atom->tag   = TAG_ATOM;
  atom->atom  = atom_str;

  return atom;
}

obj_t *make_num(long num)
{
  obj_t *obj = alloc();
  obj->tag = TAG_NUM;
  obj->num = num;
  return obj;
}

obj_t *make_pair(obj_t *car, obj_t *cdr)
{
  obj_t *obj = alloc();
  obj->tag = TAG_PAIR;
  obj->pair.car = car;
  obj->pair.cdr = cdr;
  return obj;
}

obj_t *make_clos(obj_t *body, obj_t *env)
{
  obj_t *obj = alloc();
  obj->tag = TAG_CLOS;
  obj->clos.body = body;
  obj->clos.env = env;
  return obj;
}

obj_t *make_prim(void (*func)(obj_t**))
{
  obj_t *obj = alloc();
  obj->tag = TAG_PRIM;
  obj->prim.func = func;
  return obj;
}

obj_t *intern(const char *atom_buf, uvlong atom_len)
{
  // Search for an existing matching atom
  for (obj_t *list = state->interned_atoms; list != state->null; list = list->pair.cdr) {
    assert(IS_PAIR(list));
    obj_t * elem = list->pair.car;
    assert(IS_ATOM(elem));
    if (atom_len == strlen(elem->atom) &&
        0 == memcmp(atom_buf, elem->atom, atom_len)) {
      return elem;
    }
  }

  // Not found: create a new one and push the front of the list

  obj_t *atom = make_atom(atom_buf, atom_len);
  state->interned_atoms = make_pair(atom, state->interned_atoms);
  return atom;
}

obj_t *car(obj_t *obj)
{
  if (!IS_PAIR(obj)) sysfatal("Expected pair to apply car() function");
  return obj->pair.car;
}

obj_t *cdr(obj_t *obj)
{
  if (!IS_PAIR(obj)) sysfatal("Expected pair to apply cdr() function");
  return obj->pair.cdr;
}

ushort obj_equal(obj_t *a, obj_t *b)
{
  return (a == b) || (IS_NUM(a) && IS_NUM(b) && a->num == b->num);
}

long obj_i64(obj_t *a)
{
  return IS_NUM(a) ? a->num : 0;
}

/*******************************************************************
 * Read
 ******************************************************************/

char peek(void)
{
  if (state->input_pos == state->input_len) return 0;
  return state->input_str[state->input_pos];
}

void advance(void)
{
  assert(peek()); // invalid to advance beyond the end
  state->input_pos++;
}

ushort is_white(char c)
{
  return c == ' ' || c == '\t' || c == '\n';
}

ushort is_directive(char c)
{
  return c == '\'' || c == '^' || c == '$';
}

ushort is_punctuation(char c) {
  return c == 0 || is_white(c) || is_directive(c) || c == '(' || c == ')' || c == ';';
}

void skip_white_and_comments(void)
{
    while (1) {
        char c = peek();
        if (c == 0) return; // end-of-data

        if (is_white(c)) {
            advance();
        } else if (c == ';') {
            advance();
            while ((c = peek()) != 0) {
                advance();
                if (c == '\n') break; // End of comment
            }
        } else {
            break; // Not whitespace or comment, exit loop
        }
    }
}

obj_t *read_(void);

obj_t *read_list(void)
{
  if (!state->read_stack) {
    skip_white_and_comments();
    char c = peek();
    if (c == ')') {
      advance();
      return state->null;
    }
  }
  obj_t *first = read_();
  obj_t *second = read_list();
  return make_pair(first, second);
}

ushort parse_i64(const char *str, uvlong len, long *_out)
{
  // Copy to ensure null-termination
  static char buf[20];
  memcpy(buf, str, len);
  buf[len] = '\0';

  long n = atoll(buf);
  if (n == 0 && 0 != strcmp(buf, "0")) {
    return 0;
  }
  *_out = n;
  return 1;
}

obj_t *read_scalar(void)
{
  // Otherwise, assume atom or num and read it
  uvlong start = state->input_pos;
  while (!is_punctuation(peek())) advance();

  char * str = &state->input_str[start];
  uvlong n = state->input_pos - start;

  // Is it a number?
  long num;
  if (parse_i64(str, n, &num)) { // num
    return make_num(num);
  } else { // atom
    return intern(str, n);
  }
}

obj_t *read_(void)
{
  obj_t *read_stack = state->read_stack;
  if (read_stack) {
    state->read_stack = cdr(read_stack);
    return car(read_stack);
  }

  skip_white_and_comments();

  char c = peek();
  if (!c) sysfatal("End of input: could not read_()");

  // A quote?
  if (c == '\'') {
    advance();
    return state->atom_quote;
  }

  // A push?
  if (c == '^') {
    advance();
    obj_t *s = nil;
    s = make_pair(state->atom_push, s);
    s = make_pair(read_scalar(), s);
    s = make_pair(state->atom_quote, s);
    state->read_stack = s;
    return read_(); // tail-call to dump the read stack
  }

  // A pop?
  if (c == '$') {
    advance();
    obj_t *s = nil;
    s = make_pair(state->atom_pop, s);
    s = make_pair(read_scalar(), s);
    s = make_pair(state->atom_quote, s);
    state->read_stack = s;
    return read_(); // tail-call to dump the read stack
  }

  // Read a list?
  if (c == '(') {
    advance();
    return read_list();
  } else {
    return read_scalar();
  }
}

/*******************************************************************
 * Print
 ******************************************************************/

void print_recurse(obj_t *obj);

void print_list_tail(obj_t *obj)
{
    if (obj == state->null) {
        fprint(1, ")");
        return;
    }
    if (IS_PAIR(obj)) {
        fprint(1, " ");
        print_recurse(obj->pair.car);
        print_list_tail(obj->pair.cdr);
    } else {
        fprint(1, " . ");
        print_recurse(obj);
        fprint(1, ")");
    }
}

void print_recurse(obj_t *obj)
{
    if (obj == state->null) {
        fprint(1, "()");
        return;
    }
    switch (obj->tag) {
        case TAG_ATOM: fprint(1, "%s", obj->atom); break;
        case TAG_NUM: fprint(1, "%lld", obj->num); break; // Use %lld for uvlong
        case TAG_PAIR: {
            fprint(1, "(");
            print_recurse(obj->pair.car);
            print_list_tail(obj->pair.cdr);
        } break;
        case TAG_CLOS: {
            fprint(1, "CLOSURE<");
            print_recurse(obj->clos.body);
            fprint(1, ", %p>", obj->clos.env);
        } break;
        case TAG_PRIM: {
            fprint(1, "PRIM<%p>", obj->prim.func);
        } break;
    }
}

void print_obj(obj_t *obj)
{
    print_recurse(obj);
    fprint(1, "\n");
}

/*******************************************************************
 * Environment
 ******************************************************************/

/* Environment is just a simple list of key-val (dotted) pairs */

obj_t *env_find(obj_t *env_, obj_t *key)
{
  if (!IS_ATOM(key)) sysfatal("Expected 'key' to be an atom in env_find()");

  while (env_ != state->null) {
    obj_t *kv = car(env_);
    if (key == car(kv)) {
      return cdr(kv);
    }
	env_ = cdr(env_);
  }

  sysfatal("Failed to find in key='%s' in environment", key->atom);
}

obj_t *env_define(obj_t *env, obj_t *key, obj_t *val)
{
  return make_pair(make_pair(key, val), env);
}

obj_t *env_define_prim(obj_t *env, const char *name, void (*func)(obj_t **env))
{
  return env_define(env, intern(name, strlen(name)), make_prim(func));
}

/*******************************************************************
 * Value Stack Operations
 ******************************************************************/

void push(obj_t *obj)
{
  state->stack = make_pair(obj, state->stack);
}

ushort try_pop(obj_t **_out)
{
  if (!state->stack) {
    return 0;
  }

  *_out = car(state->stack);
  state->stack = cdr(state->stack);
  return 1;
}

obj_t* pop(void)
{
  obj_t *ret = nil;
  if (!try_pop(&ret)) sysfatal("Value Stack Underflow");
  return ret;
}

/*******************************************************************
 * Eval
 ******************************************************************/

void eval(obj_t *expr, obj_t **env);

void compute(obj_t *comp, obj_t *env)
{
  if (DEBUG) {
    print("compute: ");
	print_obj(comp);
  }
  while (comp != state->null) {
    // unpack
    obj_t *cmd  = car(comp);
    comp = cdr(comp);

    if (cmd == state->atom_quote) {
      if (comp == state->null) sysfatal("Expected data following a quote form");
      push(car(comp));
      comp = cdr(comp);
      continue;
    }

    // atoms and (...) get ordinary eval
    eval(cmd, &env);
  }
}

void eval(obj_t *expr, obj_t **env)
{
  if (DEBUG) {
    print("eval: ");
	print_obj(expr);
  }

  if (IS_ATOM(expr)) {
    obj_t *val = env_find(*env, expr);
    if (IS_CLOS(val)) { // closure
      compute(val->clos.body, val->clos.env);
    } else if (IS_PRIM(val)) { // primitive
      val->prim.func(env);
    } else {
      push(val);
    }
  } else if (IS_NIL(expr) || IS_PAIR(expr)) {
    push(make_clos(expr, *env));
  } else {
    push(expr);
  }
}

/*******************************************************************
 * Primitives
 ******************************************************************/

/* Core primitives */
void prim_push(obj_t **env) { push(env_find(*env, pop())); }
void prim_pop(obj_t **env)  { obj_t *k, *v; k = pop(); v = pop(); *env = env_define(*env, k, v); }
void prim_eq(obj_t **_)     { push(obj_equal(pop(), pop()) ? state->atom_true : state->null); }
void prim_cons(obj_t **_)   { obj_t *a, *b; a = pop(); b = pop(); push(make_pair(a, b)); }
void prim_car(obj_t **_)    { push(car(pop())); }
void prim_cdr(obj_t **_)    { push(cdr(pop())); }
void prim_cswap(obj_t **_)  { if (pop() == state->atom_true) { obj_t *a, *b; a = pop(); b = pop(); push(a); push(b); } }
void prim_tag(obj_t **_)    { push(make_num(pop()->tag)); }
void prim_read(obj_t **_)   { push(read_()); }
void prim_print(obj_t **_)  { print_obj(pop()); }

/* Extra primitives */
void prim_stack(obj_t **_) { push(state->stack); }
void prim_env(obj_t **env) { push(*env); }
void prim_sub(obj_t **_)   { obj_t *a, *b; b = pop(); a = pop(); push(make_num(obj_i64(a) - obj_i64(b))); }
void prim_mul(obj_t **_)   { obj_t *a, *b; b = pop(); a = pop(); push(make_num(obj_i64(a) * obj_i64(b))); }
void prim_nand(obj_t **_)  { obj_t *a, *b; b = pop(); a = pop(); push(make_num(~(obj_i64(a) & obj_i64(b)))); }
void prim_lsh(obj_t **_)   { obj_t *a, *b; b = pop(); a = pop(); push(make_num(obj_i64(a) << obj_i64(b))); }
void prim_rsh(obj_t **_)   { obj_t *a, *b; b = pop(); a = pop(); push(make_num(obj_i64(a) >> obj_i64(b))); }

/* Low-level primitives */
void prim_ptr_state(obj_t **_)    { push(make_num((long)state)); }
void prim_ptr_read(obj_t **_)     { push(make_num(*(long*)obj_i64(pop()))); }
void prim_ptr_write(obj_t **_)    { obj_t *a, *b; b = pop(); a = pop(); *(long*)obj_i64(a) = obj_i64(b); }
void prim_ptr_to_obj(obj_t **_)   { push((obj_t*)obj_i64(pop())); }
void prim_ptr_from_obj(obj_t **_) { push(make_num((long)pop())); }

/*******************************************************************
 * Misc
 ******************************************************************/

char* load_file(const char* filename) {
    int fd = open(filename, OREAD);
    if (fd < 0) {
        sysfatal("Failed to open file: '%s'", filename);
    }

    int file_size = seek(fd, 0, 2);
    seek(fd, 0, 0);

    char* mem = malloc(file_size + 1);
    int n = read(fd, mem, file_size);
    if (n != file_size) {
        sysfatal("Failed to read file");
    }

    mem[file_size] = '\0';
    close(fd);
    return mem;
}

/*******************************************************************
 * Interpreter
 ******************************************************************/

void setup(const char *input_path)
{
  state->input_str = load_file(input_path);
  state->input_len = strlen(state->input_str);
  state->input_pos = 0;

  state->read_stack = nil;
  state->null        = make_null();

  state->interned_atoms = state->null;
  state->atom_true      = intern("t", 1);
  state->atom_quote     = intern("quote", 5);
  state->atom_push      = intern("push", 4);
  state->atom_pop       = intern("pop", 3);

  state->stack = state->null;

  obj_t *env = state->null;

  // Core primitives
  env = env_define_prim(env, "push",  &prim_push);
  env = env_define_prim(env, "pop",   &prim_pop);
  env = env_define_prim(env, "cons",  &prim_cons);
  env = env_define_prim(env, "car",   &prim_car);
  env = env_define_prim(env, "cdr",   &prim_cdr);
  env = env_define_prim(env, "eq",    &prim_eq);
  env = env_define_prim(env, "cswap", &prim_cswap);
  env = env_define_prim(env, "tag",   &prim_tag);
  env = env_define_prim(env, "read",  &prim_read);
  env = env_define_prim(env, "print", &prim_print);

  // Extra primitives
  env = env_define_prim(env, "stack", &prim_stack);
  env = env_define_prim(env, "env",   &prim_env);
  env = env_define_prim(env, "-",     &prim_sub);
  env = env_define_prim(env, "*",     &prim_mul);
  env = env_define_prim(env, "nand",  &prim_nand);
  env = env_define_prim(env, "<<",    &prim_lsh);
  env = env_define_prim(env, ">>",    &prim_rsh);

  // Low-level primitives
  env = env_define_prim(env, "ptr-state!",    &prim_ptr_state);
  env = env_define_prim(env, "ptr-read!",     &prim_ptr_read);
  env = env_define_prim(env, "ptr-write!",    &prim_ptr_write);
  env = env_define_prim(env, "ptr-to-obj!",   &prim_ptr_to_obj);
  env = env_define_prim(env, "ptr-from-obj!", &prim_ptr_from_obj);

  state->env = env;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprint(2, "usage: %s <path>\n", argv[0]);
        return 1;
    }
    setup(argv[1]);

    obj_t *obj = read_();
    compute(obj, state->env);

    return 0;
}