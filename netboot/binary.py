from typing import List, Tuple


class BinaryException(Exception):
    pass


class Binary:

    @staticmethod
    def _hex(val: int) -> str:
        out = hex(val)[2:]
        out = out.upper()
        if len(out) == 1:
            out = "0" + out
        return out

    @staticmethod
    def diff(bin1: bytes, bin2: bytes) -> List[str]:
        binlength = len(bin1)
        if binlength != len(bin2):
            raise BinaryException("Cannot diff different-sized binary blobs!")

        # First, get the list of differences
        differences: List[Tuple[int, bytes, bytes]] = []
        for i in range(binlength):
            byte1 = bin1[i]
            byte2 = bin2[i]

            if byte1 != byte2:
                differences.append((i, bytes([byte1]), bytes([byte2])))

        # Don't bother with any combination crap if we have nothing to do
        if not differences:
            return []

        # Now, combine them for easier printing
        cur_block: Tuple[int, bytes, bytes] = differences[0]
        ret: List[str] = []

        def _hexrun(val: bytes) -> str:
            return " ".join(Binary._hex(v) for v in val)

        def _output(val: Tuple[int, bytes, bytes]) -> None:
            start = val[0] - len(val[1]) + 1

            ret.append(
                f"{Binary._hex(start)}: {_hexrun(val[1])} -> {_hexrun(val[2])}"
            )

        def _combine(val: Tuple[int, bytes, bytes]) -> None:
            nonlocal cur_block

            if cur_block[0] + 1 == val[0]:
                # This is a continuation of a run
                cur_block = (
                    val[0],
                    cur_block[1] + val[1],
                    cur_block[2] + val[2],
                )
            else:
                # This is a new run
                _output(cur_block)
                cur_block = val

        # Combine and output runs of differences
        for diff in differences[1:]:
            _combine(diff)

        # Make sure we output the last difference
        _output(cur_block)

        # Return our summation
        return ret

    @staticmethod
    def _gather_differences(patches: List[str], reverse: bool) -> List[Tuple[int, bytes, bytes]]:
        # First, separate out into a list of offsets and old/new bytes
        differences: List[Tuple[int, bytes, bytes]] = []

        for patch in patches:
            start_offset, patch_contents = patch.split(':', 1)
            before, after = patch_contents.split('->')
            beforevals = [
                int(x.strip(), 16) for x in before.split(" ") if x.strip()
            ]
            aftervals = [
                int(x.strip(), 16) for x in after.split(" ") if x.strip()
            ]

            if len(beforevals) != len(aftervals):
                raise BinaryException(
                    f"Patch before and after length mismatch at "
                    f"offset {start_offset}!"
                )
            if len(beforevals) == 0:
                raise BinaryException(
                    f"Must have at least one byte to change at "
                    "offset {start_offset}!"
                )

            offset = int(start_offset.strip(), 16)
            beforebytes = bytes(beforevals)
            afterbytes = bytes(aftervals)

            for i in range(len(beforebytes)):
                differences.append(
                    (
                        offset + i,
                        beforebytes[i:(i + 1)],
                        afterbytes[i:(i + 1)],
                    )
                )

        # Now, if we're doing the reverse, just switch them
        if reverse:
            differences = [(x[0], x[2], x[1]) for x in differences]

        # Finally, return it
        return differences

    @staticmethod
    def patch(
        binary: bytes,
        patches: List[str],
        *,
        reverse: bool = False,
    ) -> bytes:
        # First, grab the differences
        differences: List[Tuple[int, bytes, bytes]] = Binary._gather_differences(patches, reverse)

        # Now, apply the changes to the binary data
        for diff in differences:
            offset, old, new = diff

            if len(binary) < offset:
                raise BinaryException(
                    f"Patch offset {Binary._hex(offset)} is beyond the end of "
                    f"the binary!"
                )
            if binary[offset:(offset + 1)] != '*' and binary[offset:(offset + 1)] != old:
                raise BinaryException(
                    f"Patch offset {Binary._hex(offset)} expecting {Binary._hex(old[0])} "
                    f"but found {Binary._hex(binary[offset])}!"
                )

            binary = binary[:offset] + new + binary[(offset + 1):]

        # Return the new data!
        return binary

    @staticmethod
    def can_patch(
        binary: bytes,
        patches: List[str],
        *,
        reverse: bool = False,
    ) -> Tuple[bool, str]:
        # First, grab the differences
        differences: List[Tuple[int, bytes, bytes]] = Binary._gather_differences(patches, reverse)

        # Now, verify the changes to the binary data
        for diff in differences:
            offset, old, _ = diff

            if len(binary) < offset:
                return (
                    False,
                    f"Patch offset {Binary._hex(offset)} is beyond the end of "
                    f"the binary!"
                )
            if binary[offset:(offset + 1)] != '*' and binary[offset:(offset + 1)] != old:
                return (
                    False,
                    f"Patch offset {Binary._hex(offset)} expecting {Binary._hex(old[0])} "
                    f"but found {Binary._hex(binary[offset])}!"
                )

        # Didn't find any problems
        return (True, "")
