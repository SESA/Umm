Contributing to Umm...
=====

This document contains the general style rules and recommended best practices for Umm... This is living document and recommendation contained within are open to debate.

## Comments

We do not subscribe a formal commenting practice (e.g., Doxygen) but we do attempt to follows some of its best practices. 	

Design and interface documentation should be contained within the corresponding header files (.h) and should conform to the following style:

```
/**
 * Title 
 * Body ... 
 */

/** Single-line alternative  */
```

The use of simple C-style comments should be limited to casual inline narrations and tags (e.g., TODO, FIXME, XXX).

```
// Nothing here is finished yet...

void foo(){ 
	// TODO(owner): implement me 	
	return;
}
```


## Naming

Naming semantics are used to imply the intended use of an interface.

+ An object's **primary** interfaces use CamelCase with a starting capital. E.g., `f->Foo(); f->FooToo();`
+ All **secondary/private** methods, utility or non-classed functions use all lowercase words separated by underscores. `return if_valid_foo(f);`
+ Member names are always all-lowercase. Private members are appended with an underscore: `private: int val_;`


## Git Commits

+ **NO MERGES**: Always rebase, never merge.
+ **FINE-GRAIN COMMITS**: A commit should capture a single feature or change. Multiple logical changes = multiple commits.
+ **NO BUILD ERRORS**: Always test that you code compiles and links before it is committed. 


## Commit Messages

**These commit message guidelines are adopted from EbbRT, which adopted them from somewhere else.**

We have general rules over how our git commit messages should be
formatted.  This leads to **more readable messages** that are easy to
follow when looking through the **project history**.

### Commit Message Format
Each commit message consists of a **header**, a **body** and a
**footer**.  The header has a special format that includes a **type**,
a **scope** and a **subject**:

```
<type>(<scope>): <subject>
<BLANK LINE>
<body>
<BLANK LINE>
<footer>
```

The **header** is should be no longer than 50
characters. Every line within the body and footer should be no more
than 72 characters.

While the **header** is required, **body** and **footer** are optional.

### Type
Should be one of the following:

* **feat**: A new feature
* **fix**: A bug fix
* **docs**: Documentation only changes
* **style**: Changes that do not affect the meaning of the code
  (white-space, formatting, missing semi-colons, etc)
* **refactor**: A code change that neither fixes a bug nor adds a feature
* **perf**: A code change that improves performance
* **test**: Adding missing tests
* **chore**: Changes to the build process or auxiliary tools and
  libraries such as documentation generation

### Scope
The scope could be anything specifying place of the commit change. For
example `$net`, `$event`, `$build`, etc...

### Subject
The subject contains succinct description of the change:

* use the imperative, present tense: "change" not "changed" nor "changes"
* don't capitalize first letter
* no dot (.) at the end

### Body
Just as in the **subject**, use the imperative, present tense:
"change" not "changed" nor "changes".  The body should include the
motivation for the change and contrast this with previous behavior.

### Footer
The footer should contain any information about **Breaking Changes**
and is also the place to reference GitHub issues that this commit
**Closes**.
