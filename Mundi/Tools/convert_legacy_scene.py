#!/usr/bin/env python3
"""Convert the Week05 Primitives scene format to the current Actors format."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Legacy .scene file")
    parser.add_argument("destination", type=Path, help="Converted .scene file")
    parser.add_argument(
        "--asset-root",
        type=Path,
        required=True,
        help="Directory used to resolve paths such as Data/apple_mid.obj",
    )
    parser.add_argument(
        "--rotation-unit",
        choices=("radians", "degrees"),
        default="radians",
        help="Unit used by legacy Rotation fields (default: radians)",
    )
    return parser.parse_args()


def to_degrees(values: list[float], rotation_unit: str) -> list[float]:
    if rotation_unit == "degrees":
        return values
    return [math.degrees(value) for value in values]


def find_material_name(asset_root: Path, mesh_path: str) -> str | None:
    obj_path = asset_root / Path(mesh_path)
    if not obj_path.is_file():
        raise FileNotFoundError(f"Referenced mesh does not exist: {obj_path}")

    with obj_path.open("r", encoding="utf-8", errors="replace") as obj_file:
        for line in obj_file:
            if line.startswith("usemtl "):
                material_name = line.removeprefix("usemtl ").strip()
                return material_name or None
    return None


def make_component(
    primitive: dict,
    component_id: int,
    material_name: str | None,
    rotation_unit: str,
) -> dict:
    material_slots = []
    if material_name:
        material_slots.append({"AssetPath": material_name, "Type": "UMaterial"})

    return {
        "Type": "UStaticMeshComponent",
        "Id": component_id,
        "ParentId": 0,
        "ObjectName": f"BenchmarkMesh_{component_id}",
        "RelativeLocation": primitive["Location"],
        "RelativeRotationEuler": to_degrees(primitive["Rotation"], rotation_unit),
        "RelativeScale": primitive["Scale"],
        "StaticMesh": primitive["ObjStaticMeshAsset"],
        "MaterialSlots": material_slots,
        "bIsActive": True,
        "bIsVisible": True,
        "bHiddenInGame": False,
        "bTickEnabled": False,
        "bCastShadows": False,
        "bGenerateOverlapEvents": False,
        "bBlockComponent": False,
        "bOverrideCollisionSetting": True,
        "CollisionEnabled_Internal": 0,
        "bEnableCollision": False,
        # Convex avoids allocating a per-component BodySetup while collision is disabled.
        "CollisionType": 3,
        "bSimulatePhysics": False,
    }


def convert_scene(source: Path, destination: Path, asset_root: Path, rotation_unit: str) -> None:
    with source.open("r", encoding="utf-8") as source_file:
        legacy = json.load(source_file)

    primitives = legacy.get("Primitives")
    if not isinstance(primitives, dict):
        raise ValueError("Legacy scene does not contain a Primitives object")

    ordered_primitives = sorted(primitives.items(), key=lambda pair: int(pair[0]))
    max_actor_id = max((int(actor_id) for actor_id, _ in ordered_primitives), default=0)

    material_names: dict[str, str | None] = {}
    actors: dict[str, dict] = {}
    for index, (actor_id, primitive) in enumerate(ordered_primitives, start=1):
        if primitive.get("Type") != "StaticMeshComp":
            raise ValueError(f"Unsupported primitive type for {actor_id}: {primitive.get('Type')}")

        mesh_path = primitive["ObjStaticMeshAsset"]
        if mesh_path not in material_names:
            material_names[mesh_path] = find_material_name(asset_root, mesh_path)

        component_id = max_actor_id + index
        component = make_component(
            primitive,
            component_id,
            material_names[mesh_path],
            rotation_unit,
        )
        actors[actor_id] = {
            "Type": "AStaticMeshActor",
            "ObjectName": f"CullingBenchmark_{actor_id}",
            "Tag": "CullingBenchmark",
            "bCanEverTick": False,
            "bActorHiddenInGame": False,
            "bActorIsActive": True,
            "RootComponentId": component_id,
            "OwnedComponents": [component],
        }

    camera = dict(legacy.get("PerspectiveCamera", {}))
    if "Rotation" in camera:
        camera["Rotation"] = to_degrees(camera["Rotation"], rotation_unit)

    converted = {
        "Version": 1,
        "NextUUID": max_actor_id + len(ordered_primitives) + 1,
        "PerspectiveCamera": camera,
        "Actors": actors,
    }

    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("w", encoding="utf-8", newline="\n") as destination_file:
        json.dump(converted, destination_file, ensure_ascii=False, separators=(",", ":"))
        destination_file.write("\n")

    print(
        f"Converted {len(actors)} actors, {len(material_names)} meshes -> {destination}"
    )


def main() -> None:
    args = parse_args()
    convert_scene(
        args.source.resolve(),
        args.destination.resolve(),
        args.asset_root.resolve(),
        args.rotation_unit,
    )


if __name__ == "__main__":
    main()
