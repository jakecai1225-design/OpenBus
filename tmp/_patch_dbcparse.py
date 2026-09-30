from pathlib import Path
path = Path(r"D:\code\openbus\sin\plugins\_shared\dbcparse.py")
text = path.read_text(encoding="utf-8")
needle = "            self.attribute_defs.append(d)\n\n\n# ------------------------------------------------------------\n#  Bit extract / fill"
if "sync_value_tables_from_signals" in text:
    print("already present")
else:
    insert = '''            self.attribute_defs.append(d)

    def sync_value_tables_from_signals(self):
        """Promote signal inline VAL_ maps into ``value_tables`` for the catalog UI.

        Most Vector DBCs only emit ``VAL_ <id> <sig> 0 "A" 1 "B" ;`` without a
        prior ``VAL_TABLE_``. Without this sync the Value tables page stays empty
        even though Messages shows those encodings on each signal.
        """
        by_content = {}
        for name, table in self.value_tables.items():
            by_content[_value_table_key(table)] = name
        for msg in self.messages.values():
            for sig in msg.signals:
                if not sig.value_table:
                    continue
                named = (sig.value_table_name or "").strip()
                if named and named in self.value_tables:
                    if not self.value_tables[named]:
                        self.value_tables[named] = dict(sig.value_table)
                    continue
                key = _value_table_key(sig.value_table)
                if key in by_content:
                    sig.value_table_name = by_content[key]
                    continue
                base = "VT_%s_%s" % (
                    _safe_vt_token(msg.name), _safe_vt_token(sig.name))
                name = base
                n = 2
                while name in self.value_tables:
                    name = "%s_%d" % (base, n)
                    n += 1
                self.value_tables[name] = dict(sig.value_table)
                sig.value_table_name = name
                by_content[key] = name


def _value_table_key(table):
    if not table:
        return ()
    return tuple(sorted((int(k), str(v)) for k, v in table.items()))


def _safe_vt_token(text):
    raw = re.sub(r"[^A-Za-z0-9_]+", "_", (text or "").strip())
    raw = raw.strip("_") or "X"
    return raw[:40]


# ------------------------------------------------------------
#  Bit extract / fill'''
    if needle not in text:
        # try CRLF
        needle2 = needle.replace("\n", "\r\n")
        insert2 = insert.replace("\n", "\r\n")
        if needle2 in text:
            text = text.replace(needle2, insert2, 1)
            path.write_text(text, encoding="utf-8")
            print("inserted CRLF")
        else:
            print("NEEDLE NOT FOUND")
            idx = text.find("Bit extract")
            print(repr(text[idx-80:idx+40]))
    else:
        text = text.replace(needle, insert, 1)
        path.write_text(text, encoding="utf-8")
        print("inserted LF")
