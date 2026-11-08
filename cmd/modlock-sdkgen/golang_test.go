package main

import (
	"fmt"
	"go/ast"
	"go/parser"
	"go/token"
	"go/types"
	"testing"
)

type enumImporter struct{ pkg *types.Package }

func (i enumImporter) Import(path string) (*types.Package, error) {
	if path != "fixture" {
		return nil, fmt.Errorf("unexpected import: %s", path)
	}
	return i.pkg, nil
}

func TestGoEnumAliasesCompileForEveryNestingDepth(t *testing.T) {
	fset := token.NewFileSet()
	fixture, err := parser.ParseFile(fset, "fixture.go", `package fixture
	type TopLevel int32
	const TopLevel_TOP_VALUE TopLevel = 1
	type Outer_Nested int32
	const Outer_NESTED_VALUE Outer_Nested = 1
	type Outer_Inner_Nested int32
	const Outer_Inner_NESTED_VALUE Outer_Inner_Nested = 1
	`, 0)
	if err != nil {
		t.Fatal(err)
	}
	pkg, err := new(types.Config).Check("fixture", fset, []*ast.File{fixture}, nil)
	if err != nil {
		t.Fatal(err)
	}
	file := &protoFile{pkg: "fixture", goPath: "fixture", goName: "fixture"}
	writer := &goWriter{imports: map[string]string{}}
	for _, example := range []struct{ name, full, value string }{
		{"TopLevel", ".fixture.TopLevel", "TOP_VALUE"},
		{"Outer_Nested", ".fixture.Outer.Nested", "NESTED_VALUE"},
		{"Outer_Inner_Nested", ".fixture.Outer.Inner.Nested", "NESTED_VALUE"},
	} {
		writer.writeEnum(&enum{
			name: example.name, full: example.full, file: file,
			values: []*enumValue{{name: example.value, number: 1}},
		})
	}
	generated, err := parser.ParseFile(fset, "generated.go",
		"package generated\nimport fixturepb \"fixture\"\n"+writer.out.String(), 0)
	if err != nil {
		t.Fatal(err)
	}
	config := types.Config{Importer: enumImporter{pkg}}
	if _, err := config.Check("generated", fset, []*ast.File{generated}, nil); err != nil {
		t.Fatalf("generated enum aliases do not compile: %v\n%s", err, writer.out.String())
	}
}
