"""Differentiable perceptual stroke optimizer + native C++ compile-plan bridge."""
from .optimizer import DifferentiableArtistConfig, optimize_manifest_to_native

__all__ = ['DifferentiableArtistConfig', 'optimize_manifest_to_native']
