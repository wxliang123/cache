def read_specific_line(file_path, line_number):
    try:
        with open(file_path, "r", encoding="utf-8") as file:
            for current_line_number, line in enumerate(file, 1):
                if current_line_number == line_number:
                    return line.strip()
            return f"Error: Line {line_number} is out of range."
    except FileNotFoundError:
        return f"Error: File not found - {file_path}"
    except Exception as e:
        return f"Error: {str(e)}"


file_path = "/home/wxl/Projects/KVCache/cache/twitter/cluster17_0"
line_number = 6576466
result = read_specific_line(file_path, line_number)

print(result)
