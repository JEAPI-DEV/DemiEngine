"""Generate a two-skin test variant without changing geometry or animation data."""
import copy
import json
import struct


def split_skin(source, destination):
    if destination.exists() or destination.is_symlink() or source.resolve() == destination.resolve():
        raise ValueError('Fixture output must be a new file, separate from its source')
    raw = source.read_bytes()
    magic, version, size = struct.unpack_from('<III', raw)
    if (magic, version, size) != (0x46546c67, 2, len(raw)):
        raise ValueError('Expected a complete GLB 2.0 model')
    length, kind = struct.unpack_from('<II', raw, 12)
    if kind != 0x4e4f534a or 20 + length > len(raw):
        raise ValueError('Expected the GLB JSON chunk first')
    document = json.loads(raw[20:20 + length])
    nodes = document['nodes']
    owners = [(i, n) for i, n in enumerate(nodes) if 'mesh' in n and 'skin' in n]
    if len(owners) != 1:
        raise ValueError('Fixture expects one skinned mesh owner')
    owner_index, owner = owners[0]
    mesh = document['meshes'][owner['mesh']]
    if len(mesh['primitives']) != 2:
        raise ValueError('Fixture expects two source mesh primitives')
    second_mesh = copy.deepcopy(mesh)
    second_mesh['primitives'] = second_mesh['primitives'][1:]
    mesh['primitives'] = mesh['primitives'][:1]
    document['meshes'].append(second_mesh)
    document['skins'].append(copy.deepcopy(document['skins'][owner['skin']]))
    second_node = copy.deepcopy(owner)
    second_node.pop('children', None)
    second_node['name'] = 'split_skin_fixture_part'
    second_node['mesh'] = len(document['meshes']) - 1
    second_node['skin'] = len(document['skins']) - 1
    new_index = len(nodes)
    # Keep the new owner beside the original under the same parent/scene.
    references = [n.get('children', []) for n in nodes]
    references += [s.get('nodes', []) for s in document.get('scenes', [])]
    for children in references:
        if owner_index in children:
            children.append(new_index)
    nodes.append(second_node)
    encoded = json.dumps(document, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    # Preserve binary chunks/accessors byte-for-byte; only ownership changes.
    tail = raw[20 + length:]
    result = (struct.pack('<III', magic, version, 20 + len(encoded) + len(tail))
              + struct.pack('<II', len(encoded), kind) + encoded + tail)
    with destination.open('xb') as output:
        output.write(result)
