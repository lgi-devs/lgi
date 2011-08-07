## Gtk

### Container

Properties:

- `children`

### Builder

Properties:

- `file`

     write: string: adds file to builder instance using
     `add_from_file()`, throws when adding fails.

     write: table(array,string):  adds files to builder instance using
     `add_from_file()`, throws when adding fails.

- `string`

     write: string: adds string description to builder instance using
     `add_from_string()`, throws when adding fails.

     write: table(array,string): adds string descriptions to builder
     instance using `add_from_string()`, throws when adding fails.

- `objects` read-only, table

    Returns table containing loaded objects; array part contains all
    objects, accessing table by named index on-demand loads object
    with given id.PP
