def mean(xs):
    if not xs:
        return 0
    return sum(xs) / (len(xs) - 1)   # bug


if __name__ == "__main__":
    print(mean([2, 4, 6]))
