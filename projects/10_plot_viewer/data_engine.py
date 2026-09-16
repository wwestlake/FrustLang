import sys
import struct
import csv

def stream_csv(filepath, column_name):
    with open(filepath, 'r', encoding='utf-8') as f:
        reader = csv.reader(f)
        headers = next(reader)
        if column_name not in headers:
            return
        col_idx = headers.index(column_name)
        count = 0
        for row in reader:
            if len(row) > col_idx:
                try:
                    val = float(row[col_idx])
                    sys.stdout.buffer.write(struct.pack('<f', val))
                    count += 1
                    if count % 100 == 0:
                        sys.stdout.buffer.flush()
                except ValueError:
                    pass
    sys.stdout.buffer.flush()

if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(1)
    fmt = sys.argv[1]
    filepath = sys.argv[2]
    col = sys.argv[3]
    if fmt == "csv":
        stream_csv(filepath, col)
