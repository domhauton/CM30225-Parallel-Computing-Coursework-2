#!/bin/bash
cd ~
rm ~/jobs/*
python3 bucs/Documents/parallel-2/bin/slurm_job_gen.py
chmod 644 ~/jobs/*
find ./jobs/cw* -exec sbatch {} \;