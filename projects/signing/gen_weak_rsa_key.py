import rsa
# run `pip install rsa` if missing this package

bits = 16
pubkey,privkey = rsa.newkeys(bits)
with open(f'rsa{bits}.pri', 'w+') as fpub, open(f'rsa{bits}.pub', 'w+') as fpriv:
    fpub.write(" ".join(str(x) for x in [pubkey.n, pubkey.e]))
    fpriv.write(" ".join(str(x) for x in [privkey.n, privkey.d]))
