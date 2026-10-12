import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        path = ROOT / 'scripts/gen_stubs.py'
        self.assertTrue(path.is_file(), 'SDK ABI generator is missing')
        spec = importlib.util.spec_from_file_location('gen_stubs', path)
        self.gen = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.gen)

    def test_exact_signature_and_namespace(self):
        text = '''namespace ABI { namespace Example {
MIDL_INTERFACE("12345678-1234-1234-1234-123456789abc")
IExample : public IInspectable {
public:
virtual HRESULT STDMETHODCALLTYPE get_Name(/* [out, retval] */ HSTRING *value) = 0;
virtual HRESULT STDMETHODCALLTYPE Set(/* [in] */ UINT32 count, const BYTE *data) = 0;
}; }}'''
        result = self.gen.generate(text, 'example.h', ['ABI::Example::IExample'])
        self.assertIn('public ABI::Example::IExample', result)
        self.assertIn('get_Name(HSTRING *value) override', result)
        self.assertIn('Set(UINT32 count, const BYTE *data) override', result)
        self.assertIn('E_NOTIMPL', result)
        self.assertIn('UNIMPL', result)

    def test_rejects_missing_interface(self):
        with self.assertRaisesRegex(ValueError, 'not found'):
            self.gen.generate('', 'x.h', ['ABI::Example::IUnknown'])

    def test_rejects_non_hresult_instead_of_guessing_abi(self):
        text = '''namespace ABI { namespace Example {
MIDL_INTERFACE("12345678-1234-1234-1234-123456789abc")
IExample : public IInspectable { public:
virtual ULONG STDMETHODCALLTYPE Wrong(void) = 0;
}; }}'''
        with self.assertRaisesRegex(ValueError, 'unsupported'):
            self.gen.generate(text, 'x.h', ['ABI::Example::IExample'])


if __name__ == '__main__':
    unittest.main()
