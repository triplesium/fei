import clang.cindex
from argparse import ArgumentParser
import sys
import os
import json
from pathlib import Path
from typing import List, Dict, Tuple
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading
from tqdm import tqdm


class ParamInfo:
    """Represents information about a method/constructor parameter."""

    def __init__(self, name: str, type_name: str):
        self.name = name or ""  # Parameter might not have a name
        self.type_name = type_name

    def __str__(self):
        return f"{self.type_name} {self.name}" if self.name else self.type_name

    def has_name(self) -> bool:
        """Check if this parameter has a name."""
        return bool(self.name)

    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON serialization."""
        return {"name": self.name, "type_name": self.type_name}


class MemberInfo:
    """Represents information about a class member (property, method, constructor, etc.)."""

    def __init__(
        self,
        name: str,
        type_name: str,
        access: str,
    ):
        self.name = name
        self.type_name = type_name
        self.access = access

    def __str__(self):
        return f"{self.name}: {self.type_name} ({self.access})"

    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON serialization."""
        return {
            "name": self.name,
            "type_name": self.type_name,
            "access": self.access,
        }


class MethodInfo(MemberInfo):
    """Represents information about a method or constructor."""

    def __init__(
        self,
        name: str,
        type_name: str,
        access: str,
        parameters: List[ParamInfo] = None,
        is_static: bool = False,
        is_const: bool = False,
        is_abstract: bool = False,
    ):
        super().__init__(name, type_name, access)
        self.parameters = parameters or []  # List of ParamInfo objects
        self.is_static = is_static
        self.is_const = is_const
        self.is_abstract = is_abstract

    def __str__(self):
        if self.parameters:
            param_str = ", ".join([str(param) for param in self.parameters])
            method_str = f"{self.name}({param_str}): {self.type_name}"
        else:
            method_str = f"{self.name}: {self.type_name}"

        # Add modifiers
        modifiers = []
        if self.is_static:
            modifiers.append("static")
        if self.is_const:
            modifiers.append("const")
        if self.is_abstract:
            modifiers.append("abstract")

        if modifiers:
            modifier_str = " ".join(modifiers)
            return f"{method_str} [{modifier_str}] ({self.access})"
        else:
            return f"{method_str} ({self.access})"

    def get_parameter_count(self) -> int:
        """Get the number of parameters for this method."""
        return len(self.parameters)

    def has_parameters(self) -> bool:
        """Check if this method has any parameters."""
        return len(self.parameters) > 0

    def get_signature(self) -> str:
        """Get the method signature without access specifier."""
        if self.parameters:
            param_str = ", ".join([str(param) for param in self.parameters])
            return f"{self.name}({param_str}): {self.type_name}"
        else:
            return f"{self.name}: {self.type_name}"

    def get_parameters_by_type(self, type_name: str) -> List[ParamInfo]:
        """Get all parameters of a specific type."""
        return [param for param in self.parameters if type_name in param.type_name]

    def has_unnamed_parameters(self) -> bool:
        """Check if this method has any unnamed parameters."""
        return any(not param.has_name() for param in self.parameters)

    def is_getter(self) -> bool:
        """Check if this method looks like a getter (const, no parameters, returns non-void)."""
        return (
            self.is_const
            and len(self.parameters) == 0
            and self.type_name != "void"
            and self.name.startswith(("get_", "is_", "has_"))
        )

    def is_setter(self) -> bool:
        """Check if this method looks like a setter (non-const, has parameters, returns void)."""
        return (
            not self.is_const
            and len(self.parameters) > 0
            and self.type_name == "void"
            and self.name.startswith("set_")
        )

    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON serialization."""
        base_dict = super().to_dict()
        base_dict["parameters"] = [param.to_dict() for param in self.parameters]
        base_dict["is_static"] = self.is_static
        base_dict["is_const"] = self.is_const
        base_dict["is_abstract"] = self.is_abstract
        return base_dict

    def to_cpp_type(self, parent: str = None) -> str:
        """Convert this method to a C++ type string."""
        param_str = ", ".join([param.type_name for param in self.parameters])
        if self.is_static:
            return f"{self.type_name}(*)({param_str})"
        else:
            ty = f"{self.type_name}({parent}::*)({param_str})"
            if self.is_const:
                ty += " const"
            return ty


class CppClassInfo:
    """Represents information about a C++ class."""

    def __init__(self, name: str, cursor: clang.cindex.Cursor, source_file: str = ""):
        self.name = name
        self.cursor = cursor
        self.source_file = source_file  # Track which file this class is from
        self.properties: List[MemberInfo] = []
        self.methods: List[MethodInfo] = []
        self.constructors: List[MethodInfo] = []
        self.access_specifier = "private"  # Default for class

    def add_property(self, name: str, type_name: str, access: str):
        """Add a property variable to the class."""
        member_info = MemberInfo(name, type_name, access)
        self.properties.append(member_info)

    def add_method(
        self,
        name: str,
        return_type: str,
        access: str,
        parameters: List[ParamInfo] = None,
        is_static: bool = False,
        is_const: bool = False,
        is_abstract: bool = False,
    ):
        """Add a method to the class."""
        method_info = MethodInfo(
            name, return_type, access, parameters, is_static, is_const, is_abstract
        )
        self.methods.append(method_info)

    def add_constructor(
        self,
        access: str,
        parameters: List[ParamInfo] = None,
        is_static: bool = False,
    ):
        """Add a constructor to the class."""
        # Constructors are never const or abstract, but could be static (factory methods)
        # Constructor return type should match the class name
        method_info = MethodInfo(
            self.name, self.name, access, parameters, is_static, False, False
        )
        self.constructors.append(method_info)

    def get_all_members(self) -> List[MemberInfo]:
        """Get all members (properties, methods, constructors) as a single list."""
        all_members = []
        all_members.extend(self.properties)
        all_members.extend(self.constructors)
        all_members.extend(self.methods)
        return all_members

    def get_members_by_access(self, access: str) -> List[MemberInfo]:
        """Get all members with a specific access level."""
        return [member for member in self.get_all_members() if member.access == access]

    def get_static_methods(self) -> List[MethodInfo]:
        """Get all static methods."""
        return [method for method in self.methods if method.is_static]

    def get_const_methods(self) -> List[MethodInfo]:
        """Get all const methods."""
        return [method for method in self.methods if method.is_const]

    def get_abstract_methods(self) -> List[MethodInfo]:
        """Get all abstract methods."""
        return [method for method in self.methods if method.is_abstract]

    def is_abstract(self) -> bool:
        """Check if this class is abstract (has any pure virtual methods)."""
        return any(method.is_abstract for method in self.methods)

    def get_getters(self) -> List[MethodInfo]:
        """Get all methods that look like getters."""
        return [method for method in self.methods if method.is_getter()]

    def get_setters(self) -> List[MethodInfo]:
        """Get all methods that look like setters."""
        return [method for method in self.methods if method.is_setter()]

    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON serialization."""
        return {
            "name": self.name,
            "source_file": self.source_file,
            "properties": [prop.to_dict() for prop in self.properties],
            "methods": [method.to_dict() for method in self.methods],
            "constructors": [ctor.to_dict() for ctor in self.constructors],
        }


class CppHeaderParser:
    """Parser for C++ header files using libclang."""

    def __init__(self, header_paths: List[str], include_paths: List[str] = None):
        self.header_paths = header_paths  # Now accepts multiple files
        self.include_paths = include_paths or []
        self.classes: List[CppClassInfo] = []
        self._lock = threading.Lock()  # Thread safety for collecting results

        # Initialize libclang - try different approaches
        self._init_libclang()

    def _init_libclang(self):
        """Initialize libclang library."""
        # The libclang package should handle this automatically
        pass

    def parse(self, max_workers: int = None) -> List[CppClassInfo]:
        """Parse the header files and extract class information using multi-threading."""

        # If max_workers is None, use the number of CPU cores
        if max_workers is None:
            max_workers = min(len(self.header_paths), os.cpu_count() or 1)

        # For single file or single thread, use sequential processing
        if len(self.header_paths) == 1 or max_workers == 1:
            return self._parse_sequential()

        # Use ThreadPoolExecutor for multi-threaded parsing
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            # Submit all header files for parsing
            future_to_header = {
                executor.submit(self._parse_single_header, header_path): header_path
                for header_path in self.header_paths
            }

            # Use tqdm for progress tracking
            with tqdm(
                total=len(self.header_paths), desc="Parsing headers", unit="file"
            ) as pbar:
                # Collect results as they complete
                for future in as_completed(future_to_header):
                    header_path = future_to_header[future]
                    try:
                        classes = future.result()
                        # Thread-safe addition to the main classes list
                        with self._lock:
                            self.classes.extend(classes)

                        # Update progress bar with file info
                        filename = Path(header_path).name
                        pbar.set_postfix_str(f"{filename}: {len(classes)} classes")
                        pbar.update(1)
                    except Exception as exc:
                        pbar.write(f"Error parsing {header_path}: {exc}")
                        pbar.update(1)

        return self.classes

    def _parse_sequential(self) -> List[CppClassInfo]:
        """Sequential parsing for single file or when multi-threading is disabled."""
        with tqdm(self.header_paths, desc="Parsing headers", unit="file") as pbar:
            for header_path in pbar:
                filename = Path(header_path).name
                pbar.set_postfix_str(f"Processing {filename}")

                classes = self._parse_single_header(header_path)
                self.classes.extend(classes)

                # Update postfix with results
                pbar.set_postfix_str(f"{filename}: {len(classes)} classes")

        return self.classes

    def _parse_single_header(self, header_path: str) -> List[CppClassInfo]:
        """Parse a single header file and return the classes found in it."""
        classes = []

        # Check if file exists
        if not os.path.exists(header_path):
            print(f"Warning: Header file '{header_path}' not found", file=sys.stderr)
            return classes

        # Set up compilation arguments
        args = ["-x", "c++-header", "-std=c++23"]

        # Add include paths
        for include_path in self.include_paths:
            args.extend(["-I", include_path])

        # Create translation unit for this header
        index = clang.cindex.Index.create()
        translation_unit = index.parse(header_path, args=args)

        # Check for parsing errors
        if translation_unit is None:
            print(f"Warning: Failed to parse {header_path}", file=sys.stderr)
            return classes

        # Print diagnostics if any
        for diagnostic in translation_unit.diagnostics:
            if diagnostic.severity >= clang.cindex.Diagnostic.Warning:
                print(
                    f"Warning in {header_path}: {diagnostic.spelling}",
                    file=sys.stderr,
                )

        # Parse the translation unit and collect classes
        return self._parse_cursor_for_header(translation_unit.cursor, header_path)

    def _parse_cursor_for_header(
        self,
        cursor: clang.cindex.Cursor,
        header_path: str,
        current_access: str = "private",
        parent_cursor: clang.cindex.Cursor = None,
    ) -> List[CppClassInfo]:
        """Recursively parse cursors to find class definitions for a specific header."""
        classes = []

        # Skip cursors that are not from the main file we're parsing
        if cursor.location.file and cursor.location.file.name != header_path:
            return classes

        # Check if this cursor represents a class or struct
        if cursor.kind == clang.cindex.CursorKind.CLASS_DECL:
            # If this is a nested class, check if it should be skipped based on access
            if parent_cursor and parent_cursor.kind in [
                clang.cindex.CursorKind.CLASS_DECL,
                clang.cindex.CursorKind.STRUCT_DECL,
            ]:
                if current_access in ["private", "protected"]:
                    return classes  # Skip private and protected nested classes
            class_info = self._parse_class_for_header(
                cursor, "private", header_path
            )  # Classes default to private
            if class_info:
                classes.append(class_info)
        elif cursor.kind == clang.cindex.CursorKind.STRUCT_DECL:
            # If this is a nested struct, check if it should be skipped based on access
            if parent_cursor and parent_cursor.kind in [
                clang.cindex.CursorKind.CLASS_DECL,
                clang.cindex.CursorKind.STRUCT_DECL,
            ]:
                if current_access in ["private", "protected"]:
                    return classes  # Skip private and protected nested structs
            class_info = self._parse_class_for_header(
                cursor, "public", header_path
            )  # Structs default to public
            if class_info:
                classes.append(class_info)
        elif cursor.kind == clang.cindex.CursorKind.CLASS_TEMPLATE:
            # Skip class templates - they're not concrete instantiable classes
            pass

        # For class/struct cursors, we need to track access specifiers for their children
        if cursor.kind in [
            clang.cindex.CursorKind.CLASS_DECL,
            clang.cindex.CursorKind.STRUCT_DECL,
        ]:
            child_access = (
                "private"
                if cursor.kind == clang.cindex.CursorKind.CLASS_DECL
                else "public"
            )
            for child in cursor.get_children():
                if child.kind == clang.cindex.CursorKind.CXX_ACCESS_SPEC_DECL:
                    child_access = self._get_access_specifier(child, header_path)
                else:
                    child_classes = self._parse_cursor_for_header(
                        child, header_path, child_access, cursor
                    )
                    classes.extend(child_classes)
        else:
            # For other cursors, recursively parse children with current access
            for child in cursor.get_children():
                child_classes = self._parse_cursor_for_header(
                    child, header_path, current_access, cursor
                )
                classes.extend(child_classes)

        return classes

    def _parse_class_for_header(
        self, class_cursor: clang.cindex.Cursor, default_access: str, header_path: str
    ) -> CppClassInfo:
        """Parse a class or struct definition and return the class info, or None if skipped."""

        # Skip forward declarations
        if not class_cursor.is_definition():
            return None

        # Skip class templates
        if class_cursor.kind == clang.cindex.CursorKind.CLASS_TEMPLATE:
            return None

        # Skip anonymous classes/structs
        class_name = class_cursor.spelling
        if not class_name:
            return None

        # Get the fully qualified class name including namespaces
        qualified_class_name = self._get_qualified_name(class_cursor)
        if not qualified_class_name:
            qualified_class_name = class_name

        # For template specializations, try to get the full name with template arguments
        # Check if this is a template specialization by looking at the display name
        display_name = class_cursor.displayname
        if display_name and display_name != class_name and "<" in display_name:
            # Use display name for template specializations as it includes template arguments
            if "::" in qualified_class_name:
                # Preserve namespace qualification
                namespace_part = qualified_class_name.rsplit("::", 1)[0]
                qualified_class_name = f"{namespace_part}::{display_name}"
            else:
                qualified_class_name = display_name

        absolute_path = Path(header_path).resolve().as_posix()
        class_info = CppClassInfo(qualified_class_name, class_cursor, absolute_path)
        current_access = default_access

        # Parse class members
        for child in class_cursor.get_children():
            if child.kind == clang.cindex.CursorKind.CXX_ACCESS_SPEC_DECL:
                # Update access specifier
                current_access = self._get_access_specifier(child, header_path)
            elif child.kind == clang.cindex.CursorKind.FIELD_DECL:
                # Property variable
                property_name = child.spelling

                # Skip properties with incomplete types
                property_type_obj = child.type
                canonical_type = property_type_obj.get_canonical()

                # Check if the type is incomplete
                if self._is_incomplete_type(canonical_type):
                    continue

                property_type = self._get_fully_qualified_type(property_type_obj)
                class_info.add_property(property_name, property_type, current_access)
            elif child.kind == clang.cindex.CursorKind.CXX_METHOD:
                # Member function - skip deleted methods
                if child.is_deleted_method():
                    continue

                method_name = child.spelling
                return_type = self._get_fully_qualified_type(child.result_type)
                parameters = self._get_parameters(child)

                # Check if method is static, const, or abstract (pure virtual)
                is_static = child.is_static_method()
                is_const = child.is_const_method()
                is_abstract = child.is_pure_virtual_method()

                class_info.add_method(
                    method_name,
                    return_type,
                    current_access,
                    parameters,
                    is_static,
                    is_const,
                    is_abstract,
                )
            elif child.kind == clang.cindex.CursorKind.CONSTRUCTOR:
                # Constructor - skip deleted constructors
                if child.is_deleted_method():
                    continue

                parameters = self._get_parameters(child)
                class_info.add_constructor(current_access, parameters)
            elif child.kind == clang.cindex.CursorKind.DESTRUCTOR:
                # Destructor
                pass

        return class_info

    def _parse_class(
        self, class_cursor: clang.cindex.Cursor, default_access: str, header_path: str
    ):
        """Parse a class or struct definition (legacy method for backward compatibility)."""
        class_info = self._parse_class_for_header(
            class_cursor, default_access, header_path
        )
        if class_info:
            self.classes.append(class_info)

    def _get_parameters(self, cursor: clang.cindex.Cursor) -> List[ParamInfo]:
        """Extract parameter information from a function/method/constructor cursor."""
        parameters = []
        for child in cursor.get_children():
            if child.kind == clang.cindex.CursorKind.PARM_DECL:
                param_name = child.spelling or ""  # Parameter might not have a name
                param_type = self._get_fully_qualified_type(child.type)
                param_info = ParamInfo(param_name, param_type)
                parameters.append(param_info)
        return parameters

    def _get_fully_qualified_type(self, type_obj) -> str:
        """Get fully qualified type name including namespaces."""
        # Start with the original spelling which preserves template arguments and aliases
        original_spelling = type_obj.spelling

        # Get the canonical type to resolve typedefs
        canonical_type = type_obj.get_canonical()
        canonical_spelling = canonical_type.spelling

        # For pointer/reference types, get the pointee type
        if canonical_type.kind == clang.cindex.TypeKind.POINTER:
            pointee = canonical_type.get_pointee()
            # Check if this is a function pointer
            if (
                pointee.kind == clang.cindex.TypeKind.FUNCTIONPROTO
                or pointee.kind == clang.cindex.TypeKind.FUNCTIONNOPROTO
            ):
                return self._format_function_pointer_type(pointee)
            return self._get_fully_qualified_type(pointee) + "*"
        elif canonical_type.kind in [
            clang.cindex.TypeKind.LVALUEREFERENCE,
            clang.cindex.TypeKind.RVALUEREFERENCE,
        ]:
            pointee = canonical_type.get_pointee()
            pointee_type_str = self._get_fully_qualified_type(pointee)

            # Determine reference operator
            ref_op = (
                "&"
                if canonical_type.kind == clang.cindex.TypeKind.LVALUEREFERENCE
                else "&&"
            )

            # Handle array references specially: float[9]& should be float(&)[9]
            if pointee.kind in [
                clang.cindex.TypeKind.CONSTANTARRAY,
                clang.cindex.TypeKind.INCOMPLETEARRAY,
            ]:
                # Split the array type into base type and array dimensions
                if "[" in pointee_type_str and "]" in pointee_type_str:
                    array_start = pointee_type_str.find("[")
                    base_type = pointee_type_str[:array_start]
                    array_part = pointee_type_str[array_start:]
                    return f"{base_type}({ref_op}){array_part}"
                else:
                    return pointee_type_str + ref_op
            else:
                return pointee_type_str + ref_op

        # For built-in types, arrays, and other non-class types, prefer original spelling
        # This preserves typedefs like GLuint, size_t, etc.
        if canonical_type.kind in [
            clang.cindex.TypeKind.VOID,
            clang.cindex.TypeKind.BOOL,
            clang.cindex.TypeKind.CHAR_U,
            clang.cindex.TypeKind.UCHAR,
            clang.cindex.TypeKind.CHAR16,
            clang.cindex.TypeKind.CHAR32,
            clang.cindex.TypeKind.USHORT,
            clang.cindex.TypeKind.UINT,
            clang.cindex.TypeKind.ULONG,
            clang.cindex.TypeKind.ULONGLONG,
            clang.cindex.TypeKind.UINT128,
            clang.cindex.TypeKind.CHAR_S,
            clang.cindex.TypeKind.SCHAR,
            clang.cindex.TypeKind.WCHAR,
            clang.cindex.TypeKind.SHORT,
            clang.cindex.TypeKind.INT,
            clang.cindex.TypeKind.LONG,
            clang.cindex.TypeKind.LONGLONG,
            clang.cindex.TypeKind.INT128,
            clang.cindex.TypeKind.FLOAT,
            clang.cindex.TypeKind.DOUBLE,
            clang.cindex.TypeKind.LONGDOUBLE,
            clang.cindex.TypeKind.CONSTANTARRAY,
            clang.cindex.TypeKind.INCOMPLETEARRAY,
            clang.cindex.TypeKind.TYPEDEF,  # Preserve typedefs like GLuint, size_t
        ]:
            return original_spelling

        # For typedefs that resolve to built-in types, prefer original spelling
        if type_obj.kind == clang.cindex.TypeKind.TYPEDEF and canonical_type.kind in [
            clang.cindex.TypeKind.VOID,
            clang.cindex.TypeKind.BOOL,
            clang.cindex.TypeKind.CHAR_U,
            clang.cindex.TypeKind.UCHAR,
            clang.cindex.TypeKind.CHAR16,
            clang.cindex.TypeKind.CHAR32,
            clang.cindex.TypeKind.USHORT,
            clang.cindex.TypeKind.UINT,
            clang.cindex.TypeKind.ULONG,
            clang.cindex.TypeKind.ULONGLONG,
            clang.cindex.TypeKind.UINT128,
            clang.cindex.TypeKind.CHAR_S,
            clang.cindex.TypeKind.SCHAR,
            clang.cindex.TypeKind.WCHAR,
            clang.cindex.TypeKind.SHORT,
            clang.cindex.TypeKind.INT,
            clang.cindex.TypeKind.LONG,
            clang.cindex.TypeKind.LONGLONG,
            clang.cindex.TypeKind.INT128,
            clang.cindex.TypeKind.FLOAT,
            clang.cindex.TypeKind.DOUBLE,
            clang.cindex.TypeKind.LONGDOUBLE,
        ]:
            return original_spelling

        # Prefer canonical spelling if it has more template information than original
        # This helps with auto-deduced types that might lose template parameters
        if (
            canonical_spelling
            and canonical_spelling.count("<") > original_spelling.count("<")
            and canonical_spelling.count(">") > original_spelling.count(">")
        ):
            return canonical_spelling

        # Try to preserve the original type name if it's already well-formed
        if original_spelling and "::" in original_spelling:
            # Original spelling already has namespace qualification
            return original_spelling

        # Get the type declaration cursor
        type_decl = canonical_type.get_declaration()
        if type_decl.kind != clang.cindex.CursorKind.NO_DECL_FOUND:
            # Build the fully qualified name by walking up the namespace hierarchy
            qualified_name = self._get_qualified_name(type_decl)
            if qualified_name:
                # If original spelling contains template arguments, try to preserve them
                if (
                    original_spelling
                    and "<" in original_spelling
                    and ">" in original_spelling
                ):
                    # Check if the base name matches
                    template_start = original_spelling.find("<")
                    base_name = original_spelling[:template_start].strip()
                    qualified_base = qualified_name.split("::")[-1]  # Get the last part

                    if base_name == qualified_base or base_name.endswith(
                        qualified_base
                    ):
                        # Replace the base name with the qualified name
                        template_part = original_spelling[template_start:]
                        return qualified_name + template_part
                    else:
                        # The names don't match, might be an alias
                        # In this case, prefer the original spelling if it looks reasonable
                        if "::" in original_spelling:
                            return original_spelling
                        else:
                            return qualified_name + template_part
                else:
                    return qualified_name

        # Fallback to the original spelling, or canonical if original is incomplete
        return original_spelling or canonical_type.spelling

    def _format_function_pointer_type(self, func_type) -> str:
        """Format a function pointer type in the form 'return_type(*)(param_types)'."""
        # Get the return type
        return_type = self._get_fully_qualified_type(func_type.get_result())

        # Get parameter types
        param_types = []

        # Try to get parameter info from the function type
        try:
            # For function types, we need to check if there are argument types available
            if hasattr(func_type, "argument_types"):
                for arg_type in func_type.argument_types():
                    param_types.append(self._get_fully_qualified_type(arg_type))
            elif hasattr(func_type, "get_arg_type") and hasattr(
                func_type, "get_num_arg_types"
            ):
                num_args = func_type.get_num_arg_types()
                for i in range(num_args):
                    arg_type = func_type.get_arg_type(i)
                    param_types.append(self._get_fully_qualified_type(arg_type))
        except:
            # If we can't get parameter types, fall back to parsing the spelling
            spelling = func_type.spelling
            if "(" in spelling and ")" in spelling:
                # Try to extract parameter part from the spelling
                start = spelling.find("(")
                end = spelling.rfind(")")
                if start != -1 and end != -1 and end > start:
                    params_part = spelling[start + 1 : end].strip()
                    if params_part and params_part != "void":
                        # Split parameters by comma, but be careful with nested types
                        param_types = [p.strip() for p in params_part.split(",")]

        # Handle variadic functions
        try:
            if (
                hasattr(func_type, "is_function_variadic")
                and func_type.is_function_variadic()
            ):
                param_types.append("...")
        except:
            pass

        # Format as function pointer: return_type(*)(param1, param2, ...)
        params_str = ", ".join(param_types) if param_types else ""
        return f"{return_type}(*)({params_str})"

    def _is_incomplete_type(self, type_obj) -> bool:
        """Check if a type is incomplete (forward declared but not defined)."""
        # Get the canonical type to resolve typedefs
        canonical_type = type_obj.get_canonical()

        # For pointer and reference types, check if the pointee is incomplete
        if canonical_type.kind in [
            clang.cindex.TypeKind.POINTER,
            clang.cindex.TypeKind.LVALUEREFERENCE,
            clang.cindex.TypeKind.RVALUEREFERENCE,
        ]:
            pointee = canonical_type.get_pointee()
            return self._is_incomplete_type(pointee)

        # For class/struct/union types, check if they have a declaration and if it's complete
        if canonical_type.kind in [
            clang.cindex.TypeKind.RECORD,
            clang.cindex.TypeKind.ENUM,
        ]:
            type_decl = canonical_type.get_declaration()
            if type_decl.kind != clang.cindex.CursorKind.NO_DECL_FOUND:
                # Check if the declaration is a definition (complete) or just a forward declaration
                return not type_decl.is_definition()

        # Built-in types and other types are always complete
        return False

    def _get_access_specifier(
        self, cursor: clang.cindex.Cursor, header_path: str
    ) -> str:
        """Get the access specifier from a CXX_ACCESS_SPEC_DECL cursor."""
        # This is a bit of a hack since libclang doesn't directly expose the access specifier
        # We parse it from the cursor's location in the source
        try:
            source_range = cursor.extent
            start_location = source_range.start
            end_location = source_range.end

            # Read the source text
            with open(header_path, "r") as f:
                lines = f.readlines()

            line_text = lines[start_location.line - 1]
            if "public" in line_text:
                return "public"
            elif "protected" in line_text:
                return "protected"
            elif "private" in line_text:
                return "private"
        except:
            pass

        return "private"  # Default

    def _get_qualified_name(self, cursor: clang.cindex.Cursor) -> str:
        """Get the fully qualified name of a cursor including namespaces."""
        if not cursor or cursor.kind == clang.cindex.CursorKind.TRANSLATION_UNIT:
            return ""

        # Get parent qualified name
        parent = cursor.semantic_parent
        parent_name = ""
        if parent and parent.kind != clang.cindex.CursorKind.TRANSLATION_UNIT:
            parent_name = self._get_qualified_name(parent)

        # Get current name
        current_name = cursor.spelling
        if not current_name:
            return parent_name

        # Combine parent and current name
        if parent_name:
            return f"{parent_name}::{current_name}"
        else:
            return current_name


def print_class_summary(classes: List[CppClassInfo]):
    """Print a summary of all found classes."""
    print("\n" + "=" * 60)
    print("CLASS SUMMARY")
    print("=" * 60)

    # Group classes by source file
    classes_by_file = {}
    for class_info in classes:
        file_name = class_info.source_file
        if file_name not in classes_by_file:
            classes_by_file[file_name] = []
        classes_by_file[file_name].append(class_info)

    for file_name, file_classes in classes_by_file.items():
        print(f"\nFile: {file_name}")
        print("=" * (len(file_name) + 6))

        for class_info in file_classes:
            print(f"\nClass: {class_info.name}")
            print("-" * (len(class_info.name) + 7))

            if class_info.properties:
                print("Properties:")
                for member in class_info.properties:
                    print(f"  - {member}")
            else:
                print("Properties: None")

            if class_info.constructors:
                print("Constructors:")
                for member in class_info.constructors:
                    print(f"  - {member}")
            else:
                print("Constructors: None")

            if class_info.methods:
                print("Methods:")
                for member in class_info.methods:
                    print(f"  - {member}")
            else:
                print("Methods: None")


def save_classes_to_json(classes: List[CppClassInfo], output_file: str):
    """Save class information to a JSON file."""
    # Convert classes to dictionary format
    classes_data = {
        "classes": [class_info.to_dict() for class_info in classes],
        "summary": {
            "total_classes": len(classes),
            "files_parsed": list(set(class_info.source_file for class_info in classes)),
        },
    }

    # Write to JSON file with pretty formatting
    with open(output_file, "w", encoding="utf-8") as f:
        json.dump(classes_data, f, indent=2, ensure_ascii=False)


def gen_cpp_file(classes: List[CppClassInfo], root_dir: Path, output_file: str):
    with open(output_file, "w", encoding="utf-8") as f:
        f.write('#include "reflgen.hpp"\n')
        f.write("// Generated by reflgen\n\n")
        includes = list(set(info.source_file for info in classes))
        f.write('#include "refl/registry.hpp"\n')
        for include in includes:
            f.write(
                f'#include "{Path(include).relative_to(root_dir.resolve()).as_posix()}"\n'
            )
        f.write("\nnamespace fei {\n")
        f.write("void register_classes() {\n")
        f.write("auto& registry = Registry::instance();\n")

        # Sort classes by name for consistency
        classes.sort(key=lambda x: x.name)
        for class_info in classes:
            if class_info.name == "fei::Registry":
                continue
            f.write(f"registry.register_cls<{class_info.name}>()")
            for prop in class_info.properties:
                if prop.access != "public":
                    continue
                f.write(
                    f'\n\t.add_property("{prop.name}", &{class_info.name}::{prop.name})'
                )
            for method in class_info.methods:
                if method.access != "public":
                    continue
                # Skip operator overloads for now
                if method.name.startswith("operator"):
                    continue
                f.write(
                    f'\n\t.add_method("{method.name}", static_cast<{method.to_cpp_type(class_info.name)}>(&{class_info.name}::{method.name}))'
                )
            # Abstract classes should not be constructible
            if not class_info.is_abstract():
                for constructor in class_info.constructors:
                    if constructor.access != "public":
                        continue
                    if constructor.has_parameters():
                        params = ", ".join(
                            [param.type_name for param in constructor.parameters]
                        )
                        f.write(f"\n\t.add_constructor<{class_info.name}, {params}>()")
                    else:
                        f.write(f"\n\t.add_constructor<{class_info.name}>()")
            f.write(";\n")
        f.write("}\n}\n")


def main():
    """Main entry point."""
    parser = ArgumentParser(
        description="Parse C++ header files and extract class information"
    )
    parser.add_argument(
        "headers", nargs="+", help="Path(s) to the C++ header file(s) to parse"
    )
    parser.add_argument(
        "-I",
        "--include",
        action="append",
        dest="includes",
        help="Include directories (can be specified multiple times)",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="Enable verbose output"
    )
    parser.add_argument(
        "--outmode",
        "-m",
        type=str,
        choices=["json", "cpp"],
        default="json",
    )
    parser.add_argument(
        "--rootdir",
        type=Path,
        default=Path.cwd(),
        help="Root directory for relative paths",
    )
    parser.add_argument(
        "--output",
        "-o",
        type=str,
        help="Output file path for JSON format (if not specified, prints to console)",
    )
    parser.add_argument(
        "--threads",
        "-j",
        type=int,
        default=None,
        help="Number of threads to use for parsing (default: number of CPU cores, 1 to disable multi-threading)",
    )

    args = parser.parse_args()

    # Check if header files exist
    missing_files = []
    for header in args.headers:
        if not os.path.exists(header):
            missing_files.append(header)

    if missing_files:
        print(f"Error: The following header files were not found:", file=sys.stderr)
        for file in missing_files:
            print(f"  - {file}", file=sys.stderr)
        sys.exit(1)

    try:
        # Create parser and parse the headers
        parser_instance = CppHeaderParser(args.headers, args.includes or [])
        classes = parser_instance.parse(max_workers=args.threads)

        # Print summary
        total_files = len(args.headers)
        total_classes = len(classes)
        print(
            f"\n✅ Parsing complete! Found {total_classes} classes in {total_files} files."
        )

        if not classes:
            print("No classes found in the header files.")
        else:
            if args.output:
                if args.outmode == "cpp":
                    # Generate C++ file
                    gen_cpp_file(classes, args.rootdir, args.output)
                    print(f"📝 Generated C++ reflection code: {args.output}")
                else:
                    # Save to JSON file
                    save_classes_to_json(classes, args.output)
                    print(f"💾 Saved class information: {args.output}")
            else:
                # Print to console
                print_class_summary(classes)

    except Exception as e:
        print(f"Error parsing headers: {e}", file=sys.stderr)
        if args.verbose:
            import traceback

            traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
