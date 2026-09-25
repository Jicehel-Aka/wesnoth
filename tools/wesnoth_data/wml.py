"""wml.py — préprocesseur et parseur WML (Wesnoth Markup Language).

Couvre ce dont on a besoin pour lire les vraies données de Wesnoth :
  * directives : #define/#enddef (avec arguments), #ifdef/#ifndef/#else/#endif,
    #undef, commentaires, #textdomain ;
  * appels de macro {NAME args...} avec arguments entre parenthèses, chaînes,
    macros imbriquées ; expansion récursive des macros connues ;
  * macros « intégrées » (fonctions Python) pour les macros du cœur de
    Wesnoth que nous n'avons pas en source (ON_DIFFICULTY, GENERIC_UNIT, ...) ;
  * tags [x]...[/x], [+x] (ajout au dernier [x] frère) ;
  * clés multiples « a,b,c=1,2,3 », chaînes traduisibles _ "..." sur plusieurs
    lignes, concaténation par +, guillemets doublés "" ;
  * les macros inconnues restent dans l'arbre sous forme de marqueurs
    (Node.macros) : c'est ainsi qu'on repère par exemple {TRAIT_STRONG}.
"""
import os
import re


class Node:
    __slots__ = ("tag", "attrs", "children", "macros")

    def __init__(self, tag):
        self.tag = tag
        self.attrs = {}
        self.children = []
        self.macros = []   # [(name, [args])] des macros non développées

    def get(self, k, d=None):
        return self.attrs.get(k, d)

    def all(self, tag):
        return [c for c in self.children if c.tag == tag]

    def first(self, tag):
        for c in self.children:
            if c.tag == tag:
                return c
        return None

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()

    def __repr__(self):
        return f"<{self.tag} {self.attrs} {len(self.children)} enfants>"


# ---------------------------------------------------------------------------
# Découpage d'un appel de macro en arguments
# ---------------------------------------------------------------------------
def split_macro_call(inner):
    """'NAME a (b c) "d e" {X y}' -> ('NAME', ['a', 'b c', '"d e"', '{X y}'])."""
    args, i, n = [], 0, len(inner)
    buf = []

    def flush():
        if buf:
            args.append("".join(buf))
            buf.clear()

    while i < n:
        ch = inner[i]
        if ch.isspace():
            flush(); i += 1; continue
        if ch == "(":
            depth, j = 1, i + 1
            while j < n and depth:
                if inner[j] == '"':
                    j = _skip_string(inner, j)
                    continue
                if inner[j] == "(":
                    depth += 1
                elif inner[j] == ")":
                    depth -= 1
                j += 1
            flush()
            args.append(inner[i + 1:j - 1])
            i = j
            continue
        if ch == '"':
            j = _skip_string(inner, i)
            buf.append(inner[i:j]); i = j
            continue
        if ch == "{":
            j = _match_brace(inner, i)
            buf.append(inner[i:j]); i = j
            continue
        buf.append(ch); i += 1
    flush()
    if not args:
        return "", []
    return args[0], args[1:]


def _skip_string(s, i):
    """i pointe sur un guillemet ouvrant ; renvoie l'index après le fermant."""
    j = i + 1
    while j < len(s):
        if s[j] == '"':
            if j + 1 < len(s) and s[j + 1] == '"':
                j += 2
                continue
            return j + 1
        j += 1
    return j


def _match_brace(s, i):
    depth, j = 0, i
    while j < len(s):
        c = s[j]
        if c == '"':
            j = _skip_string(s, j)
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return j + 1
        j += 1
    return j


# ---------------------------------------------------------------------------
# Préprocesseur
# ---------------------------------------------------------------------------
class Preprocessor:
    def __init__(self, defines=("NORMAL", "CAMPAIGN_THE_SOUTH_GUARD"), builtins=None):
        self.symbols = set(defines)
        self.macros = {}          # nom -> (params, corps)
        self.builtins = dict(builtins or {})
        self.unknown = {}         # macros rencontrées sans définition -> nb

    # -- chargement de définitions sans rien produire ----------------------
    def load_defines(self, path):
        self.process(open(path, encoding="utf-8").read())

    # -- passe 1 : directives et commentaires --------------------------------
    def strip_directives(self, text):
        out = []
        lines = text.split("\n")
        stack = []          # pile d'états actifs #ifdef
        i = 0
        in_string = False
        while i < len(lines):
            line = lines[i]
            active = all(stack)
            st = line.strip()
            if not in_string and st.startswith("#"):
                word = st.split()[0] if st.split() else "#"
                rest = st[len(word):].strip()
                if word in ("#ifdef", "#ifndef", "#ifhave", "#ifnhave", "#ifver", "#ifnver"):
                    name = rest.split()[0] if rest else ""
                    if word == "#ifdef":
                        cond = name in self.symbols or name in self.macros
                    elif word == "#ifndef":
                        cond = not (name in self.symbols or name in self.macros)
                    elif word in ("#ifhave", "#ifver"):
                        cond = True
                    else:
                        cond = False
                    stack.append(cond); i += 1; continue
                if word == "#else":
                    if stack:
                        stack[-1] = not stack[-1]
                    i += 1; continue
                if word == "#endif":
                    if stack:
                        stack.pop()
                    i += 1; continue
                if word == "#define":
                    parts = rest.split()
                    name, params = parts[0], parts[1:]
                    body = []
                    # corps sur une seule ligne : "#define X\n30#enddef" est
                    # aussi possible (fin collée à la valeur)
                    i += 1
                    while i < len(lines):
                        l2 = lines[i]
                        if "#enddef" in l2:
                            before = l2[:l2.index("#enddef")]
                            if before.strip():
                                body.append(before)
                            break
                        # #arg ... #endarg : valeur par défaut d'un argument
                        body.append(l2)
                        i += 1
                    i += 1
                    if active:
                        self.macros[name] = ([p for p in params if not p.startswith("#")],
                                             self.strip_directives("\n".join(body)))
                    continue
                if word == "#undef":
                    # Ignoré volontairement : l'expansion se fait APRÈS le
                    # retrait des directives (deux passes), et les fichiers
                    # d'unités font #undef en fin de fichier de macros qu'ils
                    # viennent d'utiliser. Une redéfinition ultérieure écrase.
                    i += 1; continue
                # tout autre "#..." est un commentaire (dont #textdomain, #po:)
                i += 1
                continue
            if active:
                out.append(self._strip_comment(line, in_string))
                in_string = self._ends_in_string(line, in_string)
            else:
                in_string = False
            i += 1
        return "\n".join(out)

    @staticmethod
    def _ends_in_string(line, in_string):
        for ch in line:
            if ch == '"':
                in_string = not in_string
            elif ch == "#" and not in_string:
                break
        return in_string

    @staticmethod
    def _strip_comment(line, in_string):
        res = []
        for ch in line:
            if ch == '"':
                in_string = not in_string
            elif ch == "#" and not in_string:
                break
            res.append(ch)
        return "".join(res)

    # -- passe 2 : expansion des macros --------------------------------------
    def expand(self, text, depth=0):
        if depth > 40:
            return text
        out, i, n = [], 0, len(text)
        while i < n:
            c = text[i]
            if c == '"':
                j = _skip_string(text, i)
                out.append(text[i:j]); i = j; continue
            if c == "{":
                j = _match_brace(text, i)
                inner = text[i + 1:j - 1].strip()
                out.append(self._expand_call(inner, text[i:j], depth))
                i = j
                continue
            out.append(c); i += 1
        return "".join(out)

    def _expand_call(self, inner, raw, depth):
        name, args = split_macro_call(inner)
        # {./fichier} ou {~chemin} : inclusions -> ignorées ici
        if not name or name.startswith(("~", ".", "/")) or "/" in name:
            return ""
        args = [self.expand(a, depth + 1) for a in args]
        if name in self.builtins:
            res = self.builtins[name](self, args)
            if res is not None:
                return self.expand(res, depth + 1)
        if name in self.macros:
            params, body = self.macros[name]
            for p, a in zip(params, args):
                body = body.replace("{" + p + "}", a)
            return self.expand(body, depth + 1)
        self.unknown[name] = self.unknown.get(name, 0) + 1
        # macro inconnue : conservée comme marqueur (arguments normalisés)
        return "{" + " ".join([name] + [("(" + a + ")") for a in args]) + "}"

    def process(self, text):
        return self.expand(self.strip_directives(text))


# ---------------------------------------------------------------------------
# Parseur de l'arbre
# ---------------------------------------------------------------------------
_TAG = re.compile(r"\[(/?)(\+?)([A-Za-z0-9_]+)\]")


def _clean_value(v):
    """Chaîne WML -> texte : retire _, guillemets, concatène les '+'."""
    v = v.strip()
    parts, i, n = [], 0, len(v)
    cur = []
    while i < n:
        c = v[i]
        if c == '"':
            j = _skip_string(v, i)
            cur.append(v[i + 1:j - 1].replace('""', '"'))
            i = j; continue
        if c == "+":
            parts.append("".join(cur)); cur = []; i += 1; continue
        if c == "_" and (i + 1 < n and v[i + 1] in ' "'):
            i += 1; continue
        if c.isspace() and not cur:
            i += 1; continue
        cur.append(c); i += 1
    parts.append("".join(cur))
    return "".join(p.strip() if not p.startswith(("\n",)) else p for p in parts).strip()


def parse(text):
    root = Node("root")
    stack = [root]
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1; continue
        if c == "[":
            m = _TAG.match(text, i)
            if m:
                closing, plus, name = m.group(1), m.group(2), m.group(3)
                i = m.end()
                if closing:
                    # fermeture : remonter jusqu'au tag correspondant (tolérant)
                    for k in range(len(stack) - 1, 0, -1):
                        if stack[k].tag == name:
                            del stack[k:]
                            break
                    continue
                parent = stack[-1]
                if plus:
                    prev = None
                    for ch in reversed(parent.children):
                        if ch.tag == name:
                            prev = ch; break
                    if prev is None and parent.tag == name:
                        # [+unit] écrit à l'intérieur même de [unit] (macro
                        # développée dans le corps) : s'applique au tag courant
                        prev = parent
                    node = prev or Node(name)
                    if prev is None:
                        parent.children.append(node)
                else:
                    node = Node(name)
                    parent.children.append(node)
                stack.append(node)
                continue
        if c == "{":
            j = _match_brace(text, i)
            name, args = split_macro_call(text[i + 1:j - 1])
            stack[-1].macros.append((name, args))
            i = j
            continue
        # attribut : clé(s)=valeur jusqu'à fin de ligne hors chaîne,
        # prolongé si la ligne suivante commence par '+' ou finit par '+'
        eq = text.find("=", i)
        nl = text.find("\n", i)
        if eq == -1 or (nl != -1 and nl < eq):
            # ligne sans '=' (bruit) : sauter
            i = n if nl == -1 else nl + 1
            continue
        keys = text[i:eq].strip()
        j = eq + 1
        in_str = False
        while j < n:
            ch = text[j]
            if ch == '"':
                if in_str and j + 1 < n and text[j + 1] == '"':
                    j += 2; continue
                in_str = not in_str
            elif ch == "\n" and not in_str:
                # continuation par '+'
                rest = text[j + 1:].lstrip(" \t")
                if text[eq + 1:j].rstrip().endswith("+") or rest.startswith("+"):
                    j += 1; continue
                break
            elif ch in "[{" and not in_str:
                # plusieurs éléments sur une même ligne ([a][/a] key=v ...)
                pre = text[eq + 1:j].strip()
                if pre and not pre.endswith(("+", "_")):
                    break
            j += 1
        raw = text[eq + 1:j]
        i = j
        if not re.fullmatch(r"[A-Za-z0-9_,\s]+", keys):
            continue
        ks = [k.strip() for k in keys.split(",")]
        if len(ks) == 1:
            stack[-1].attrs[ks[0]] = _clean_value(raw)
        else:
            vs = _split_top_commas(raw)
            for idx, k in enumerate(ks):
                if idx < len(vs):
                    stack[-1].attrs[k] = _clean_value(vs[idx]) if idx < len(ks) - 1 \
                        else _clean_value(",".join(vs[idx:]))
    return root


def _split_top_commas(s):
    out, cur, in_str = [], [], False
    for ch in s:
        if ch == '"':
            in_str = not in_str
        if ch == "," and not in_str:
            out.append("".join(cur)); cur = []
        else:
            cur.append(ch)
    out.append("".join(cur))
    return out


def load(path, pp):
    return parse(pp.process(open(path, encoding="utf-8").read()))
